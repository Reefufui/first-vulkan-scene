echo "compiling shaders..."
glslangValidator -V scene.frag -o scene.frag.spv
glslangValidator -V scene.vert -o scene.vert.spv
glslangValidator -V shadowmap.vert -o shadowmap.vert.spv
glslangValidator -V shadowmap.frag -o shadowmap.frag.spv
