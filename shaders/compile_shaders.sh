echo "compiling shaders..."
glslangValidator -V scene.frag -o scene.frag.spv
glslangValidator -V scene.vert -o scene.vert.spv
glslangValidator -V shadowmap.vert -o shadowmap.vert.spv
glslangValidator -V shadowmap.frag -o shadowmap.frag.spv
glslangValidator -V showcubemap.vert -o showcubemap.vert.spv
glslangValidator -V showcubemap.frag -o showcubemap.frag.spv
glslangValidator -V particle.frag -o particle.frag.spv
glslangValidator -V particle.vert -o particle.vert.spv
glslangValidator -V gbuffer.frag -o gbuffer.frag.spv
glslangValidator -V gbuffer.vert -o gbuffer.vert.spv
glslangValidator -V ssao.frag -o ssao.frag.spv
glslangValidator -V ssao.vert -o ssao.vert.spv
