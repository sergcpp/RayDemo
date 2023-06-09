import binascii
import os
import sys
import subprocess
import threading

ENABLE_MULTIPLE_THREADS = True
ENABLE_SPIRV_OPTIMIZATION = True
ENABLE_DXC_COMPILATION = (os.name == "nt")

mutex = threading.Lock()

def spirv_base_path():
    if sys.platform.startswith("linux"):
        return os.path.join("third-party", "spirv", "linux")
    elif sys.platform == "darwin":
        return os.path.join("third-party", "spirv", "macos")
    elif os.name == "nt":
        return os.path.join("third-party", "spirv", "win32")

def dxc_base_path():
    if sys.platform.startswith("linux"):
        return os.path.join("third-party", "dxc", "linux")
    elif os.name == "nt":
        return os.path.join("third-party", "dxc", "win32")

def make_sublist_group(lst: list, grp: int) -> list:
    return [lst[i:i+grp] for i in range(0, len(lst), grp)]

def do_conversion(content: bytes) -> str:
    hexstr = binascii.hexlify(content).decode("UTF-8")
    hexstr = hexstr.upper()
    array = ["0x" + hexstr[i:i + 2] + "" for i in range(0, len(hexstr), 2)]
    array = make_sublist_group(array, 64)
    ret = "\n    ".join([", ".join(e) + "," for e in array])
    return ret[0:len(ret) - 1]

def bin2header(data, file_name):
    file_name = file_name.replace(os.sep, '/')
    ret = "/* Contents of file " + file_name + " */\n"
    ret += "const long int " + file_name.replace('/', '_').replace('.', '_') + "_size = " + str(len(data)) + ";\n"
    ret += "const unsigned char " + file_name.replace('/', '_').replace('.', '_') + "[" + str(len(data)) + "] = {\n    "
    ret += do_conversion(data)
    ret += "\n};\n"
    return ret

def compile_shader(src_name, spv_name=None, glsl_version=None, target_env="spirv1.3", defines="", hlsl_profile="cs_6_0"):
    if  spv_name == None:
        spv_name = src_name[0:-4] + "spv"

    hlsl_name = spv_name[0:-3] + "hlsl"
    cso_name = spv_name[0:-3] + "cso"

    compile_cmd = os.path.join(spirv_base_path(), "glslangValidator -V --target-env " + target_env + " internal/shaders/" + src_name + " " + defines + " -o internal/shaders/output/" + spv_name)
    if (glsl_version != None):
        compile_cmd += " --glsl-version " + glsl_version
    compile_result = subprocess.run(compile_cmd, shell=True, capture_output=True, text=True, check=False)
    spirv_opt_result = None
    spirv_cross_result = None
    dxc_result = None
    if ENABLE_SPIRV_OPTIMIZATION == True:
        if os.name == "nt":
            spirv_opt_result = subprocess.run(os.path.join(spirv_base_path(), "spirv-opt.bat internal/shaders/output/" + spv_name + " -o internal/shaders/output/" + spv_name), shell=True, capture_output=True, text=True, check=False)
        else:
            spirv_opt_result = subprocess.run(os.path.join(spirv_base_path(), "spirv-opt.sh internal/shaders/output/" + spv_name + " -o internal/shaders/output/" + spv_name), shell=True, capture_output=True, text=True, check=False)
    if ENABLE_DXC_COMPILATION == True and hlsl_profile != None:
        spirv_cross_result = subprocess.run(os.path.join(spirv_base_path(), "spirv-cross internal/shaders/output/" + spv_name + " --hlsl --shader-model 60 --output internal/shaders/output/" + hlsl_name), shell=True, capture_output=True, text=True, check=False)
        dxc_result = subprocess.run(os.path.join(dxc_base_path(), "dxc -T " + hlsl_profile + " internal/shaders/output/" + hlsl_name + " -Fo internal/shaders/output/" + cso_name), shell=True, capture_output=True, text=True, check=False)

        if dxc_result.returncode == 0:
            app_refl_result = subprocess.run(os.path.join(dxc_base_path(), "append_refl_data internal/shaders/output/", cso_name), shell=True, capture_output=True, text=True, check=False)
            if app_refl_result.returncode == 0:
                with open(os.path.join("internal", "shaders", "output", cso_name), 'rb') as f:
                    cso_data = f.read()
                out = bin2header(cso_data, os.path.join("internal", "shaders", "output", cso_name))
                with open(os.path.join("internal", "shaders", "output", cso_name + ".inl"), 'w') as f:
                    f.write(out)

    if compile_result.returncode == 0:
        with open(os.path.join("internal", "shaders", "output", spv_name), 'rb') as f:
            spv_data = f.read()
        out = bin2header(spv_data, os.path.join("internal", "shaders", "output", spv_name))
        with open(os.path.join("internal", "shaders", "output", spv_name + ".inl"), 'w') as f:
            f.write(out)

    mutex.acquire()
    try:
        print(compile_result.stdout)
        if spirv_opt_result != None:
            print(spirv_opt_result.stdout)
        if spirv_cross_result != None:
            print(spirv_cross_result.stdout)
        if dxc_result != None:
            print(dxc_result.stdout)
    finally:
        mutex.release()

def compile_shader_async(src_name, spv_name=None, glsl_version=None, target_env="spirv1.3", defines = "", hlsl_profile="cs_6_0"):
    if ENABLE_MULTIPLE_THREADS == True:
        threading.Thread(target=compile_shader, args=(src_name, spv_name, glsl_version, target_env, defines, hlsl_profile,)).start()
    else:
        compile_shader(src_name, spv_name, glsl_version, target_env, defines, hlsl_profile)

def main():
    for item in os.listdir("internal/shaders/output"):
        if item.endswith(".spv") or item.endswith(".spv.inl") or (ENABLE_DXC_COMPILATION and (item.endswith(".hlsl") or item.endswith(".cso") or item.endswith(".cso.inl"))):
            os.remove(os.path.join("internal/shaders/output", item))

    # Primary ray generation
    compile_shader_async(src_name="primary_ray_gen.comp.glsl", spv_name="primary_ray_gen_simple.comp.spv", defines="-DADAPTIVE=0")
    compile_shader_async(src_name="primary_ray_gen.comp.glsl", spv_name="primary_ray_gen_adaptive.comp.spv", defines="-DADAPTIVE=1")

    # Scene intersection (main, inline RT)
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_swrt_atlas.comp.spv", defines="-DINDIRECT=0 -DHWRT=0 -DBINDLESS=0")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_swrt_bindless.comp.spv", defines="-DINDIRECT=0 -DHWRT=0 -DBINDLESS=1")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_hwrt_atlas.comp.spv", glsl_version="460", target_env="spirv1.4", defines="-DINDIRECT=0 -DHWRT=1 -DBINDLESS=0", hlsl_profile="cs_6_5")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_hwrt_bindless.comp.spv", glsl_version="460", target_env="spirv1.4", defines="-DINDIRECT=0 -DHWRT=1 -DBINDLESS=1", hlsl_profile="cs_6_5")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_indirect_swrt_atlas.comp.spv", defines="-DINDIRECT=1 -DHWRT=0 -DBINDLESS=0")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_indirect_swrt_bindless.comp.spv", defines="-DINDIRECT=1 -DHWRT=0 -DBINDLESS=1")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_indirect_hwrt_atlas.comp.spv", glsl_version="460", target_env="spirv1.4", defines="-DINDIRECT=1 -DHWRT=1 -DBINDLESS=0", hlsl_profile="cs_6_5")
    compile_shader_async(src_name="intersect_scene.comp.glsl", spv_name="intersect_scene_indirect_hwrt_bindless.comp.spv", glsl_version="460", target_env="spirv1.4", defines="-DINDIRECT=1 -DHWRT=1 -DBINDLESS=1", hlsl_profile="cs_6_5")
    # Scene intersection (main, pipeline RT)
    compile_shader_async(src_name="intersect_scene.rgen.glsl", spv_name="intersect_scene.rgen.spv", glsl_version="460", target_env="spirv1.4", defines="-DINDIRECT=0 -DBINDLESS=1", hlsl_profile=None)
    compile_shader_async(src_name="intersect_scene.rgen.glsl", spv_name="intersect_scene_indirect.rgen.spv", glsl_version="460", target_env="spirv1.4", defines="-DINDIRECT=1 -DBINDLESS=1", hlsl_profile=None)
    compile_shader_async(src_name="intersect_scene.rchit.glsl", spv_name="intersect_scene.rchit.spv", glsl_version="460", target_env="spirv1.4", hlsl_profile=None)
    compile_shader_async(src_name="intersect_scene.rmiss.glsl", spv_name="intersect_scene.rmiss.spv", glsl_version="460", target_env="spirv1.4", hlsl_profile=None)

    # Lights intersection
    compile_shader_async(src_name="intersect_area_lights.comp.glsl", defines="-DPRIMARY=0")

    # Shading
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_atlas.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=0 -DOUTPUT_BASE_COLOR=0 -DOUTPUT_DEPTH_NORMALS=0")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_atlas_n.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=0 -DOUTPUT_BASE_COLOR=0 -DOUTPUT_DEPTH_NORMALS=1")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_atlas_b.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=0 -DOUTPUT_BASE_COLOR=1 -DOUTPUT_DEPTH_NORMALS=0")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_atlas_bn.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=0 -DOUTPUT_BASE_COLOR=1 -DOUTPUT_DEPTH_NORMALS=1")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_bindless.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=1 -DOUTPUT_BASE_COLOR=0 -DOUTPUT_DEPTH_NORMALS=0")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_bindless_n.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=1 -DOUTPUT_BASE_COLOR=0 -DOUTPUT_DEPTH_NORMALS=1")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_bindless_b.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=1 -DOUTPUT_BASE_COLOR=1 -DOUTPUT_DEPTH_NORMALS=0")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_primary_bindless_bn.comp.spv", defines="-DPRIMARY=1 -DINDIRECT=1 -DBINDLESS=1 -DOUTPUT_BASE_COLOR=1 -DOUTPUT_DEPTH_NORMALS=1")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_secondary_atlas.comp.spv", defines="-DPRIMARY=0 -DINDIRECT=1 -DBINDLESS=0")
    compile_shader_async(src_name="shade.comp.glsl", spv_name="shade_secondary_bindless.comp.spv", defines="-DPRIMARY=0 -DINDIRECT=1 -DBINDLESS=1")

    # Scene intersection (shadow)
    compile_shader_async(src_name="intersect_scene_shadow.comp.glsl", spv_name="intersect_scene_shadow_swrt_atlas.comp.spv", defines="-DHWRT=0 -DBINDLESS=0")
    compile_shader_async(src_name="intersect_scene_shadow.comp.glsl", spv_name="intersect_scene_shadow_swrt_bindless.comp.spv", defines="-DHWRT=0 -DBINDLESS=1")
    compile_shader_async(src_name="intersect_scene_shadow.comp.glsl", spv_name="intersect_scene_shadow_hwrt_atlas.comp.spv", glsl_version="460", target_env="spirv1.4", defines="-DHWRT=1 -DBINDLESS=0", hlsl_profile="cs_6_5")
    compile_shader_async(src_name="intersect_scene_shadow.comp.glsl", spv_name="intersect_scene_shadow_hwrt_bindless.comp.spv", glsl_version="460", target_env="spirv1.4", defines="-DHWRT=1 -DBINDLESS=1", hlsl_profile="cs_6_5")

    # Postprocess
    compile_shader_async(src_name="mix_incremental.comp.glsl", spv_name="mix_incremental.comp.spv", defines="-DOUTPUT_BASE_COLOR=0 -DOUTPUT_DEPTH_NORMALS=0")
    compile_shader_async(src_name="mix_incremental.comp.glsl", spv_name="mix_incremental_n.comp.spv", defines="-DOUTPUT_BASE_COLOR=0 -DOUTPUT_DEPTH_NORMALS=1")
    compile_shader_async(src_name="mix_incremental.comp.glsl", spv_name="mix_incremental_b.comp.spv", defines="-DOUTPUT_BASE_COLOR=1 -DOUTPUT_DEPTH_NORMALS=0")
    compile_shader_async(src_name="mix_incremental.comp.glsl", spv_name="mix_incremental_bn.comp.spv", defines="-DOUTPUT_BASE_COLOR=1 -DOUTPUT_DEPTH_NORMALS=1")
    compile_shader_async(src_name="postprocess.comp.glsl")

    # Denoise
    compile_shader_async(src_name="filter_variance.comp.glsl")
    compile_shader_async(src_name="nlm_filter.comp.glsl", spv_name="nlm_filter.comp.spv", defines="-DUSE_BASE_COLOR=0 -DUSE_DEPTH_NORMAL=0")
    compile_shader_async(src_name="nlm_filter.comp.glsl", spv_name="nlm_filter_n.comp.spv", defines="-DUSE_BASE_COLOR=0 -DUSE_DEPTH_NORMAL=1")
    compile_shader_async(src_name="nlm_filter.comp.glsl", spv_name="nlm_filter_b.comp.spv", defines="-DUSE_BASE_COLOR=1 -DUSE_DEPTH_NORMAL=0")
    compile_shader_async(src_name="nlm_filter.comp.glsl", spv_name="nlm_filter_bn.comp.spv", defines="-DUSE_BASE_COLOR=1 -DUSE_DEPTH_NORMAL=1")

    # Sorting
    compile_shader_async(src_name="sort_hash_rays.comp.glsl")
    compile_shader_async(src_name="sort_scan.comp.glsl", spv_name="sort_inclusive_scan.comp.spv", defines="-DEXCLUSIVE_SCAN=0")
    compile_shader_async(src_name="sort_scan.comp.glsl", spv_name="sort_exclusive_scan.comp.spv", defines="-DEXCLUSIVE_SCAN=1")
    compile_shader_async(src_name="sort_add_partial_sums.comp.glsl")
    compile_shader_async(src_name="sort_init_count_table.comp.glsl")
    compile_shader_async(src_name="sort_write_sorted_hashes.comp.glsl")
    compile_shader_async(src_name="sort_reorder_rays.comp.glsl")

    # Other
    compile_shader_async(src_name="prepare_indir_args.comp.glsl")
    compile_shader_async(src_name="debug_rt.comp.glsl", target_env="spirv1.4", hlsl_profile="cs_6_5")

if __name__ == "__main__":
    main()
