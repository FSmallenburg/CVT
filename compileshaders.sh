#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

bgfx_dir=${BGFX_DIR:-"${script_dir}/third_party/bgfx"}
bgfx_build_dir=${BGFX_BUILD_DIR:-"${bgfx_dir}/.build/linux64_gcc"}
shaderc=${SHADERC:-"${bgfx_build_dir}/bin/shadercRelease"}
bgfx_include_dir=${BGFX_SHADER_INCLUDE_DIR:-"${bgfx_dir}/src"}
shader_platform=${BGFX_SHADER_PLATFORM:-linux}
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
