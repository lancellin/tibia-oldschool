# Gera git_version.h com o estado git do momento do build.
# Invocado em todo build; só reescreve o arquivo se o conteúdo mudou,
# para não forçar recompilação/relink desnecessários.
# Variáveis esperadas: GIT_EXECUTABLE, SOURCE_DIR, OUT_HEADER

set(SERVER_GIT_TAG "untagged")
set(SERVER_GIT_SHA "unknown")
set(SERVER_GIT_DIRTY 0)

if(GIT_EXECUTABLE)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
		WORKING_DIRECTORY ${SOURCE_DIR}
		OUTPUT_VARIABLE _tag OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET RESULT_VARIABLE _tag_rc)
	if(_tag_rc EQUAL 0 AND NOT _tag STREQUAL "")
		set(SERVER_GIT_TAG "${_tag}")
	endif()

	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY ${SOURCE_DIR}
		OUTPUT_VARIABLE _sha OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET RESULT_VARIABLE _sha_rc)
	if(_sha_rc EQUAL 0 AND NOT _sha STREQUAL "")
		set(SERVER_GIT_SHA "${_sha}")
	endif()

	execute_process(
		COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
		WORKING_DIRECTORY ${SOURCE_DIR}
		OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
	if(NOT _status STREQUAL "")
		set(SERVER_GIT_DIRTY 1)
	endif()
endif()

set(_content "// Gerado automaticamente no build — não editar.\n")
string(APPEND _content "#pragma once\n")
string(APPEND _content "static constexpr auto SERVER_GIT_TAG = \"${SERVER_GIT_TAG}\";\n")
string(APPEND _content "static constexpr auto SERVER_GIT_SHA = \"${SERVER_GIT_SHA}\";\n")
string(APPEND _content "static constexpr bool SERVER_GIT_DIRTY = ${SERVER_GIT_DIRTY};\n")

if(EXISTS "${OUT_HEADER}")
	file(READ "${OUT_HEADER}" _old)
	if(_old STREQUAL _content)
		return()
	endif()
endif()

file(WRITE "${OUT_HEADER}" "${_content}")
