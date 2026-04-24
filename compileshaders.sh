#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

bgfx_dir=${BGFX_DIR:-"${script_dir}/third_party/bgfx.cmake/bgfx"}
bgfx_build_dir=${BGFX_BUILD_DIR:-}
bgfx_include_dir=${BGFX_SHADER_INCLUDE_DIR:-"${bgfx_dir}/src"}
bgfx_shaderc_autobuild=${BGFX_SHADERC_AUTOBUILD:-1}
bgfx_shaderc_build_dir=${BGFX_SHADERC_BUILD_DIR:-"${script_dir}/build-shaderc"}

if [[ -n "${BGFX_SHADER_PLATFORM:-}" ]]; then
	shader_platform=${BGFX_SHADER_PLATFORM}
else
	case "$(uname -s)" in
		Darwin) shader_platform=osx ;;
		MINGW*|MSYS*|CYGWIN*) shader_platform=windows ;;
		*) shader_platform=linux ;;
	esac
fi

find_shaderc() {
	local candidate
	local candidates=()

	if [[ -n "${SHADERC:-}" ]]; then
		candidates+=("${SHADERC}")
	fi

	if [[ -n "${bgfx_build_dir}" ]]; then
		candidates+=(
			"${bgfx_build_dir}/bin/shadercRelease"
			"${bgfx_build_dir}/bin/shadercDebug"
			"${bgfx_build_dir}/bin/shaderc"
			"${bgfx_build_dir}/shaderc"
		)
	fi

	candidates+=("${bgfx_dir}/tools/bin/${shader_platform}/shaderc")
	candidates+=(
		"${script_dir}/build-debug/shaderc"
		"${script_dir}/build-release/shaderc"
		"${script_dir}/build-debug/bin/shaderc"
		"${script_dir}/build-release/bin/shaderc"
		"${bgfx_shaderc_build_dir}/shaderc"
		"${bgfx_shaderc_build_dir}/bin/shaderc"
		"${bgfx_shaderc_build_dir}/bin/shadercRelease"
		"${bgfx_shaderc_build_dir}/bin/shadercDebug"
	)

	for bgfx_candidate_bin_dir in "${bgfx_dir}"/.build/*/bin; do
		[[ -d "${bgfx_candidate_bin_dir}" ]] || continue
		candidates+=(
			"${bgfx_candidate_bin_dir}/shadercRelease"
			"${bgfx_candidate_bin_dir}/shadercDebug"
			"${bgfx_candidate_bin_dir}/shaderc"
		)
	done

	for candidate in "${candidates[@]}"; do
		if [[ -x "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
		if [[ "${candidate}" != *.exe && -x "${candidate}.exe" ]]; then
			echo "${candidate}.exe"
			return 0
		fi
	done

	if command -v shaderc >/dev/null 2>&1; then
		command -v shaderc
		return 0
	fi

	return 1
}

build_shaderc() {
	local shaderc_source_root=${BGFX_SHADERC_SOURCE_ROOT:-"${script_dir}/third_party/bgfx.cmake"}
	local shaderc_cmake_generator=${BGFX_SHADERC_CMAKE_GENERATOR:-}
	if [[ -z "${shaderc_cmake_generator}" && -n "${CMAKE_GENERATOR:-}" ]]; then
		shaderc_cmake_generator=${CMAKE_GENERATOR}
	fi
	if [[ -z "${shaderc_cmake_generator}" && "${shader_platform}" == "windows" ]] && command -v ninja >/dev/null 2>&1; then
		shaderc_cmake_generator=Ninja
	fi
	local configure_cmd=(
		cmake
		-S "${shaderc_source_root}"
		-B "${bgfx_shaderc_build_dir}"
		-DBGFX_BUILD_EXAMPLES=OFF
		-DBGFX_BUILD_TOOLS=ON
		-DBGFX_BUILD_TOOLS_SHADER=ON
		-DBGFX_BUILD_TOOLS_TEXTURE=OFF
		-DBGFX_BUILD_TOOLS_GEOMETRY=OFF
		-DBGFX_BUILD_TOOLS_BIN2C=OFF
		-DBGFX_BUILD_TESTS=OFF
		-DBGFX_INSTALL=OFF
		-DBGFX_CUSTOM_TARGETS=OFF
	)

	if [[ -n "${shaderc_cmake_generator}" ]]; then
		configure_cmd+=( -G "${shaderc_cmake_generator}" )
	fi

	# If a stale cache from a prior (possibly different-generator) run exists, wipe it
	# so CMake does not reject the new generator and produce a no-op build.
	if [[ -f "${bgfx_shaderc_build_dir}/CMakeCache.txt" ]]; then
		local cached_gen
		cached_gen=$(grep -m1 '^CMAKE_GENERATOR:' "${bgfx_shaderc_build_dir}/CMakeCache.txt" 2>/dev/null | sed 's/.*=//' || true)
		if [[ -n "${shaderc_cmake_generator}" && "${cached_gen}" != "${shaderc_cmake_generator}" ]]; then
			echo "Stale build-shaderc cache uses '${cached_gen}', reconfiguring with '${shaderc_cmake_generator}' — wiping ${bgfx_shaderc_build_dir}" >&2
			rm -rf "${bgfx_shaderc_build_dir}"
		fi
	fi

	echo "shaderc not found; attempting to build shaderc in ${bgfx_shaderc_build_dir}" >&2
	"${configure_cmd[@]}" >&2
	cmake --build "${bgfx_shaderc_build_dir}" --target shaderc >&2

	local candidate
	for candidate in \
		"${bgfx_shaderc_build_dir}/shaderc" \
		"${bgfx_shaderc_build_dir}/bin/shaderc" \
		"${bgfx_shaderc_build_dir}/bin/shadercRelease" \
		"${bgfx_shaderc_build_dir}/bin/shadercDebug"; do
		if [[ -x "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
		if [[ "${candidate}" != *.exe && -x "${candidate}.exe" ]]; then
			echo "${candidate}.exe"
			return 0
		fi
	done

	# find may return Windows-style paths on MSYS; convert before testing executability
	while IFS= read -r candidate; do
		[[ -z "${candidate}" ]] && continue
		# Normalise Windows backslash paths to forward slashes for bash
		candidate="${candidate//\\//}"
		if [[ -f "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
	done < <(find "${bgfx_shaderc_build_dir}" -type f \( -name shaderc -o -name shadercRelease -o -name shadercDebug -o -name shaderc.exe -o -name shadercRelease.exe -o -name shadercDebug.exe \) 2>/dev/null || true)

	return 1
}

if ! shaderc=$(find_shaderc); then
	if [[ "${bgfx_shaderc_autobuild}" == "1" ]]; then
		shaderc=$(build_shaderc) || {
			echo "Failed to build or locate shaderc." >&2
			echo "Set SHADERC to an existing shaderc binary, or set BGFX_SHADERC_AUTOBUILD=0 to disable auto-build." >&2
			exit 1
		}
	else
		echo "shaderc not found. Set SHADERC, BGFX_BUILD_DIR, or enable auto-build via BGFX_SHADERC_AUTOBUILD=1." >&2
		exit 1
	fi
fi

shader_profile=${BGFX_SHADER_PROFILE:-120}
# bgfx shaderc defaults to no optimization; enable an optimized build by default.
shader_optimization_level=${BGFX_SHADER_OPTIMIZATION_LEVEL:-3}
# bgfx shaderc supports desktop GLSL profiles up to 150; keep this overridable.
structure_factor_shader_profile=${BGFX_STRUCTURE_FACTOR_SHADER_PROFILE:-150}
d3d_shader_profile=${BGFX_D3D_SHADER_PROFILE:-s_5_0}
include_args=(--varyingdef "${script_dir}/shaders/varying.def.sc" -i "${script_dir}/shaders" -i "${bgfx_include_dir}")

if [[ ! -d "${bgfx_include_dir}" ]]; then
	echo "bgfx shader include directory not found at ${bgfx_include_dir}" >&2
	exit 1
fi

if [[ -n "${BGFX_SHADER_BACKENDS:-}" ]]; then
	read -r -a shader_backends <<< "${BGFX_SHADER_BACKENDS}"
else
	case "${shader_platform}" in
		windows) shader_backends=(glsl dxbc) ;;
		osx) shader_backends=(glsl metal) ;;
		*) shader_backends=(glsl) ;;
	esac
fi

if [[ ${#shader_backends[@]} -eq 0 ]]; then
	echo "No shader backends selected. Set BGFX_SHADER_BACKENDS to one or more of: glsl dxbc metal" >&2
	exit 1
fi

backend_platform() {
	local backend=$1
	case "${backend}" in
		glsl) echo "${shader_platform}" ;;
		dxbc) echo "windows" ;;
		metal)
			case "${shader_platform}" in
				osx|ios) echo "${shader_platform}" ;;
				*) echo "osx" ;;
			esac
			;;
		*)
			echo "Unsupported shader backend: ${backend}" >&2
			return 1
			;;
	esac
}

backend_profile() {
	local backend=$1
	local profile_kind=$2
	case "${backend}" in
		glsl)
			if [[ "${profile_kind}" == "structure" ]]; then
				echo "${structure_factor_shader_profile}"
			else
				echo "${shader_profile}"
			fi
			;;
		dxbc)
			echo "${d3d_shader_profile}"
			;;
		metal)
			echo "metal"
			;;
		*)
			echo "Unsupported shader backend: ${backend}" >&2
			return 1
			;;
	esac
}

compile_shader() {
	local input_file=$1
	local output_file=$2
	local shader_type=$3
	local backend=$4
	local profile_kind=$5
	local shader_backend_platform
	shader_backend_platform=$(backend_platform "${backend}")
	local shader_profile_value
	shader_profile_value=$(backend_profile "${backend}" "${profile_kind}")
	local output_dir="${script_dir}/shaders/${backend}"
	mkdir -p "${output_dir}"

	local cmd=(
		"${shaderc}"
		-f "${script_dir}/shaders/${input_file}"
		-o "${output_dir}/${output_file}"
		--type "${shader_type}"
		--platform "${shader_backend_platform}"
		-p "${shader_profile_value}"
		-O "${shader_optimization_level}"
		"${include_args[@]}"
	)

	printf '+ '
	printf '%q ' "${cmd[@]}"
	printf '\n'

	"${cmd[@]}"
}

echo "Compiling shaders for backends: ${shader_backends[*]}"

for backend in "${shader_backends[@]}"; do
	compile_shader "vs_instancing.sc" "vs_instancing.bin" vertex "${backend}" regular
	compile_shader "fs_instancing.sc" "fs_instancing.bin" fragment "${backend}" regular
	compile_shader "vs_picking.sc" "vs_picking.bin" vertex "${backend}" regular
	compile_shader "fs_picking.sc" "fs_picking.bin" fragment "${backend}" regular
	compile_shader "vs_lines.sc" "vs_lines.bin" vertex "${backend}" regular
	compile_shader "fs_lines.sc" "fs_lines.bin" fragment "${backend}" regular
	compile_shader "vs_structure_factor.sc" "vs_structure_factor.bin" vertex "${backend}" structure
	compile_shader "fs_structure_factor.sc" "fs_structure_factor.bin" fragment "${backend}" structure
	compile_shader "fs_structure_factor_color.sc" "fs_structure_factor_color.bin" fragment "${backend}" structure
done
