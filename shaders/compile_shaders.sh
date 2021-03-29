echo "compiling shaders..."
glslangValidator -V vertex.vert -o vertex.vert.spv
glslangValidator -V fragment.frag -o fragment.frag.spv
glslangValidator -V tri_mesh.vert -o tri_mesh.vert.spv
