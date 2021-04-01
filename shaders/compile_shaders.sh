echo "compiling shaders..."
glslangValidator -V scene.frag -o scene.frag.spv
glslangValidator -V scene.vert -o scene.vert.spv
