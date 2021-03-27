echo "compiling shaders..."
glslangValidator -V vertex.vert -o vert.spv
glslangValidator -V fragment.frag -o frag.spv
