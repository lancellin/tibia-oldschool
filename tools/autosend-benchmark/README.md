# Microbenchmark do autosend atual

Este teste mede isoladamente o núcleo da rotina atual: percorrer o vetor de
protocolos, verificar se há buffer pendente e chamar o caminho de envio quando
há. Ele não usa uma lista `dirty` e não modifica o TFS.

Casos:

- 100, 500, 1.000 e 10.000 protocolos;
- 0%, 1%, 5%, 10%, 50% e 100% com buffer pendente;
- 2.000 amostras por combinação, com aquecimento prévio.

Execução:

```powershell
cd D:\tibia-oldschool
.\tools\autosend-benchmark\Run-AutosendBenchmark.ps1
```

O CSV é salvo em `performance-results\autosend-microbenchmark`.

O resultado isola o custo de varredura, indireção por `shared_ptr`, branch e
chamada simulada. Criptografia, cópia de pacote, socket, kernel e rede ficam
para a medição integrada do TFS, portanto o microbenchmark não deve ser usado
como estimativa do custo total de enviar dados.
