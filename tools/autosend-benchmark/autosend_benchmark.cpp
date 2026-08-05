#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

volatile std::uint64_t sendSink = 0;

struct MockProtocol {
	bool pending = false;
	std::uint64_t token = 0;

	void send()
	{
		// Represents the call reached only by protocols with a current buffer.
		// Socket/encryption cost deliberately belongs to the integrated test.
		sendSink += token;
	}
};

struct Result {
	std::size_t protocols = 0;
	double configuredPendingPercent = 0;
	std::size_t pendingProtocols = 0;
	std::size_t samples = 0;
	std::size_t scansPerSample = 0;
	double averageMicroseconds = 0;
	double p95Microseconds = 0;
	double p99Microseconds = 0;
	double maximumMicroseconds = 0;
	double nanosecondsPerProtocol = 0;
};

double percentile(const std::vector<double>& sorted, double fraction)
{
	const std::size_t index = std::min(
		sorted.size() - 1,
		static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size()))) - 1);
	return sorted[index];
}

Result benchmark(std::size_t protocolCount, double pendingPercent)
{
	std::vector<std::shared_ptr<MockProtocol>> protocols;
	protocols.reserve(protocolCount);
	for (std::size_t i = 0; i < protocolCount; ++i) {
		auto protocol = std::make_shared<MockProtocol>();
		protocol->token = i + 1;
		protocols.emplace_back(std::move(protocol));
	}

	std::vector<std::size_t> indexes(protocolCount);
	std::iota(indexes.begin(), indexes.end(), 0);
	std::mt19937_64 random(0x772ULL + protocolCount + static_cast<std::uint64_t>(pendingPercent * 100));
	std::shuffle(indexes.begin(), indexes.end(), random);
	const std::size_t pendingCount = static_cast<std::size_t>(
		std::llround(static_cast<double>(protocolCount) * pendingPercent / 100.0));
	for (std::size_t i = 0; i < pendingCount; ++i) {
		protocols[indexes[i]]->pending = true;
	}

	constexpr std::size_t sampleCount = 2000;
	const std::size_t scansPerSample = std::max<std::size_t>(1, 100000 / protocolCount);

	const auto scan = [&]() {
		for (const auto& protocol : protocols) {
			if (protocol->pending) {
				protocol->send();
			}
		}
	};

	for (std::size_t i = 0; i < 200; ++i) {
		scan();
	}

	std::vector<double> samples;
	samples.reserve(sampleCount);
	for (std::size_t sample = 0; sample < sampleCount; ++sample) {
		const auto startedAt = std::chrono::steady_clock::now();
		for (std::size_t scanIndex = 0; scanIndex < scansPerSample; ++scanIndex) {
			scan();
		}
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - startedAt);
		const double perScanMicroseconds =
			(static_cast<double>(elapsed.count()) / static_cast<double>(scansPerSample)) / 1000.0;
		samples.emplace_back(perScanMicroseconds);
	}

	std::sort(samples.begin(), samples.end());
	Result result;
	result.protocols = protocolCount;
	result.configuredPendingPercent = pendingPercent;
	result.pendingProtocols = pendingCount;
	result.samples = sampleCount;
	result.scansPerSample = scansPerSample;
	result.averageMicroseconds = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
	result.p95Microseconds = percentile(samples, 0.95);
	result.p99Microseconds = percentile(samples, 0.99);
	result.maximumMicroseconds = samples.back();
	result.nanosecondsPerProtocol = (result.averageMicroseconds * 1000.0) / protocolCount;
	return result;
}

} // namespace

int main(int argc, char** argv)
{
	std::string outputPath = "autosend-microbenchmark.csv";
	if (argc == 3 && std::string(argv[1]) == "--output") {
		outputPath = argv[2];
	} else if (argc != 1) {
		std::cerr << "Usage: autosend_benchmark.exe [--output result.csv]\n";
		return 2;
	}

	const std::vector<std::size_t> protocolCounts{100, 500, 1000, 10000};
	const std::vector<double> pendingPercentages{0, 1, 5, 10, 50, 100};
	std::vector<Result> results;

	for (const std::size_t protocolCount : protocolCounts) {
		for (const double pendingPercent : pendingPercentages) {
			Result result = benchmark(protocolCount, pendingPercent);
			results.emplace_back(result);
			std::cout << std::setw(5) << protocolCount << " protocols, "
			          << std::setw(6) << pendingPercent << "% pending: avg="
			          << std::fixed << std::setprecision(3) << result.averageMicroseconds
			          << "us p95=" << result.p95Microseconds
			          << "us p99=" << result.p99Microseconds
			          << "us max=" << result.maximumMicroseconds << "us\n";
		}
	}

	std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
	if (!output) {
		std::cerr << "Unable to create " << outputPath << '\n';
		return 1;
	}
	output << "protocols,configured_pending_pct,pending_protocols,samples,scans_per_sample,"
	          "avg_us,p95_us,p99_us,max_us,avg_ns_per_protocol\n";
	output << std::fixed << std::setprecision(6);
	for (const Result& result : results) {
		output << result.protocols << ','
		       << result.configuredPendingPercent << ','
		       << result.pendingProtocols << ','
		       << result.samples << ','
		       << result.scansPerSample << ','
		       << result.averageMicroseconds << ','
		       << result.p95Microseconds << ','
		       << result.p99Microseconds << ','
		       << result.maximumMicroseconds << ','
		       << result.nanosecondsPerProtocol << '\n';
	}

	std::cout << "CSV: " << outputPath << "\n";
	std::cout << "Guard sink: " << sendSink << "\n";
	return 0;
}
