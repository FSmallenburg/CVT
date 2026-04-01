#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

bgfx_dir=${BGFX_DIR:-"${script_dir}/third_party/bgfx"}
bgfx_build_dir=${BGFX_BUILD_DIR:-}
if [[ -z "${bgfx_build_dir}" ]]; then
	for bgfx_candidate_bin_dir in "${bgfx_dir}"/.build/*/bin; do
		if [[ ! -d "${bgfx_candidate_bin_dir}" ]]; then
			continue
		fi
		if [[ -x "${bgfx_candidate_bin_dir}/shadercRelease" || -x "${bgfx_candidate_bin_dir}/shadercRelease.exe" ]]; then
			if [[ -n "${bgfx_build_dir}" ]]; then
				echo "Multiple bgfx build directories found under ${bgfx_dir}/.build. Set BGFX_BUILD_DIR explicitly." >&2
				exit 1
			fi
			bgfx_build_dir=$(dirname "${bgfx_candidate_bin_dir}")
		fi
	done
fi
shaderc=${SHADERC:-"${bgfx_build_dir}/bin/shadercRelease"}
if [[ ! -x "${shaderc}" && -x "${shaderc}.exe" ]]; then
	shaderc="${shaderc}.exe"
fi
bgfx_include_dir=${BGFX_SHADER_INCLUDE_DIR:-"${bgfx_dir}/src"}
if [[ -n "${BGFX_SHADER_PLATFORM:-}" ]]; then
	shader_platform=${BGFX_SHADER_PLATFORM}
else
	case "$(uname -s)" in
		Darwin) shader_platform=osx ;;
		MINGW*|MSYS*|CYGWIN*) shader_platform=windows ;;
		*) shader_platform=linux ;;
	esac
fi
shader_profile=${BGFX_SHADER_PROFILE:-120}

if [[ ! -x "${shaderc}" ]]; then
	echo "shadercRelease not found at ${shaderc}" >&2
	exit 1
fi

if [[ ! -d "${bgfx_include_dir}" ]]; then
	echo "bgfx shader include directory not found at ${bgfx_include_dir}" >&2
	exit 1
fi

"${shaderc}" -f "${script_dir}/shaders/vs_instancing.sc" -o "${script_dir}/shaders/vs_instancing.bin" --type vertex --platform "${shader_platform}" -p "${shader_profile}" -i "${bgfx_include_dir}"
"${shaderc}" -f "${script_dir}/shaders/fs_instancing.sc" -o "${script_dir}/shaders/fs_instancing.bin" --type fragment --platform "${shader_platform}" -p "${shader_profile}" -i "${bgfx_include_dir}"
"${shaderc}" -f "${script_dir}/shaders/vs_picking.sc" -o "${script_dir}/shaders/vs_picking.bin" --type vertex --platform "${shader_platform}" -p "${shader_profile}" -i "${bgfx_include_dir}"
"${shaderc}" -f "${script_dir}/shaders/fs_picking.sc" -o "${script_dir}/shaders/fs_picking.bin" --type fragment --platform "${shader_platform}" -p "${shader_profile}" -i "${bgfx_include_dir}"
"${shaderc}" -f "${script_dir}/shaders/vs_lines.sc" -o "${script_dir}/shaders/vs_lines.bin" --type vertex --platform "${shader_platform}" -p "${shader_profile}" -i "${bgfx_include_dir}"
"${shaderc}" -f "${script_dir}/shaders/fs_lines.sc" -o "${script_dir}/shaders/fs_lines.bin" --type fragment --platform "${shader_platform}" -p "${shader_profile}" -i "${bgfx_include_dir}"
