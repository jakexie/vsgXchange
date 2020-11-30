char defaultshader_vert[] = "#version 450\n"
                            "#pragma import_defines ( VSG_NORMAL, VSG_COLOR, VSG_TEXCOORD0, VSG_LIGHTING )\n"
                            "#extension GL_ARB_separate_shader_objects : enable\n"
                            "layout(push_constant) uniform PushConstants {\n"
                            "    mat4 projection;\n"
                            "    mat4 moddelview;\n"
                            "    //mat3 normal;\n"
                            "} pc;\n"
                            "layout(location = 0) in vec3 osg_Vertex;\n"
                            "#ifdef VSG_NORMAL\n"
                            "layout(location = 1) in vec3 osg_Normal;\n"
                            "layout(location = 1) out vec3 normalDir;\n"
                            "#endif\n"
                            "#ifdef VSG_COLOR\n"
                            "layout(location = 3) in vec4 osg_Color;\n"
                            "layout(location = 3) out vec4 vertColor;\n"
                            "#endif\n\n"
                            "#ifdef VSG_TEXCOORD0\n"
                            "layout(location = 4) in vec2 osg_MultiTexCoord0;\n"
                            "layout(location = 4) out vec2 texCoord0;\n"
                            "#endif\n\n"
                            "#ifdef VSG_LIGHTING\n"
                            "layout(location = 5) out vec3 viewDir;\n"
                            "layout(location = 6) out vec3 lightDir;\n"
                            "#endif\n"
                            "out gl_PerVertex{ vec4 gl_Position; };\n"
                            "\n"
                            "void main()\n"
                            "{\n"
                            "    gl_Position = (pc.projection * pc.modelview) * vec4(osg_Vertex, 1.0);\n"
                            "#ifdef VSG_TEXCOORD0\n"
                            "    texCoord0 = osg_MultiTexCoord0.st;\n"
                            "#endif\n"
                            "#ifdef VSG_NORMAL\n"
                            "    vec3 n = ((pc.mdoelview) * vec4(osg_Normal, 0.0)).xyz;\n"
                            "    normalDir = n;\n"
                            "#endif\n"
                            "#ifdef VSG_LIGHTING\n"
                            "    vec4 lpos = /*osg_LightSource.position*/ vec4(0.0, 0.25, 1.0, 0.0);\n"
                            "    viewDir = -vec3((pc.modelview) * vec4(osg_Vertex, 1.0));\n"
                            "    if (lpos.w == 0.0)\n"
                            "        lightDir = lpos.xyz;\n"
                            "    else\n"
                            "        lightDir = lpos.xyz + viewDir;\n"
                            "#endif\n"
                            "#ifdef VSG_COLOR\n"
                            "    vertColor = osg_Color;\n"
                            "#endif\n"
                            "}\n"
                            "\n";