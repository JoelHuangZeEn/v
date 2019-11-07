#include "OutlineProgram.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

Scene::Drawable::Pipeline outline_program_pipeline;

Load< OutlineProgram0 > outline_program0(LoadTagEarly, []() -> OutlineProgram0 const * {
	OutlineProgram0 *ret = new OutlineProgram0();

	//----- build the pipeline template -----
	basic_material_program_pipeline.program = ret->program;

	basic_material_program_pipeline.OBJECT_TO_CLIP_mat4 = ret->OBJECT_TO_CLIP_mat4;
	basic_material_program_pipeline.OBJECT_TO_LIGHT_mat4x3 = ret->OBJECT_TO_LIGHT_mat4x3;
	basic_material_program_pipeline.NORMAL_TO_LIGHT_mat3 = ret->NORMAL_TO_LIGHT_mat3;

	//make a 1-pixel white texture to bind by default:
	GLuint tex;
	glGenTextures(1, &tex);

	glBindTexture(GL_TEXTURE_2D, tex);
	std::vector< glm::u8vec4 > tex_data(1, glm::u8vec4(0xff));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	basic_material_program_pipeline.textures[0].texture = tex;
	basic_material_program_pipeline.textures[0].target = GL_TEXTURE_2D;

	return ret;
});

OutlineProgram0::OutlineProgram0() {
	//Compile vertex and fragment shaders using the convenient 'gl_compile_program' helper function:
  program = gl_compile_program(
		//vertex shader:
		"#version 330\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"uniform mat4x3 OBJECT_TO_LIGHT;\n"
		"uniform mat3 NORMAL_TO_LIGHT;\n"
		"in vec4 Position;\n"
		"in vec3 Normal;\n"
		"in vec4 Color;\n"
		"in vec2 TexCoord;\n"
		"out vec3 light_position;\n"
		"out vec3 light_normal;\n"
		"out vec4 color;\n"
		"out vec2 texCoord;\n"
    "out vec4 normal\n"
		"void main() {\n"
		"	gl_Position = OBJECT_TO_CLIP * Position;\n"
		"	light_position = OBJECT_TO_LIGHT * Position;\n"
		"	light_normal = NORMAL_TO_LIGHT * Normal;\n"
		"	color = Color;\n"
		"	texCoord = TexCoord;\n"
    " normal = Normal;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"uniform sampler2D TEX;\n"
		"uniform float ROUGHNESS;\n"
		"uniform int LIGHT_TYPE;\n"
		"uniform vec3 LIGHT_LOCATION;\n"
		"uniform vec3 LIGHT_DIRECTION;\n"
		"uniform vec3 LIGHT_ENERGY;\n"
		"uniform float LIGHT_CUTOFF;\n"
		"uniform vec3 EYE;\n"
		"in vec3 light_position;\n"
		"in vec3 light_normal;\n"
		"in vec4 color;\n"
		"in vec2 texCoord;\n"
    "in vec3 normal\n"
		"layout(location=0) out vec4 fragColor;\n"
    "layout(location=1) out vec4 fragNormalZ;\n"
		"void main() {\n"
		"	float shininess = pow(1024.0, 1.0 - ROUGHNESS);\n"
		"	vec4 albedo = texture(TEX, texCoord) * color;\n"
		"	vec3 n = normalize(light_normal);\n"
		"	vec3 v = normalize(EYE - light_position);\n"
		"	vec3 l; //direction to light\n"
		"	vec3 h; //half-vector\n"
		"	vec3 e; //light flux\n"
		"	if (LIGHT_TYPE == 0) { //point light \n"
		"		l = (LIGHT_LOCATION - light_position);\n"
		"		float dis2 = dot(l,l);\n"
		"		l = normalize(l);\n"
		"		h = normalize(l+v);\n"
		"		float nl = max(0.0, dot(n, l)) / max(1.0, dis2);\n"
		"		e = nl * LIGHT_ENERGY;\n"
		"	} else if (LIGHT_TYPE == 1) { //hemi light \n"
		"		l = -LIGHT_DIRECTION;\n"
		"		h = vec3(0.0); //no specular from hemi for now\n"
		"		e = (dot(n,l) * 0.5 + 0.5) * LIGHT_ENERGY;\n"
		"	} else if (LIGHT_TYPE == 2) { //spot light \n"
		"		l = (LIGHT_LOCATION - light_position);\n"
		"		float dis2 = dot(l,l);\n"
		"		l = normalize(l);\n"
		"		h = normalize(l+v);\n"
		"		float nl = max(0.0, dot(n, l)) / max(1.0, dis2);\n"
		"		float c = dot(l,-LIGHT_DIRECTION);\n"
		"		nl *= smoothstep(LIGHT_CUTOFF,mix(LIGHT_CUTOFF,1.0,0.1), c);\n"
		"		e = nl * LIGHT_ENERGY;\n"
		"	} else { //(LIGHT_TYPE == 3) //directional light \n"
		"		l = -LIGHT_DIRECTION;\n"
		"		h = normalize(l+v);\n"
		"		e = max(0.0, dot(n,l)) * LIGHT_ENERGY;\n"
		"	}\n"
		"	vec3 reflectance =\n"
		"		albedo.rgb / 3.1415926 //Lambertian Diffuse\n"
		"		+ pow(max(0.0, dot(n, h)), shininess) //Blinn-Phong Specular\n"
		"		  * (shininess + 2.0) / (8.0) //normalization factor\n"
		"		  * mix(0.04, 1.0, pow(1.0 - max(0.0, dot(h, v)), 5.0)) //Schlick's approximation for Fresnel reflectance\n"
		"	;\n"
		"	fragColor = vec4(e*reflectance, albedo.a);\n"
    " fragNormalZ = vec4(normalize(normal), gl_FragCoord.z);\n"
		"}\n"
	);
	//As you can see above, adjacent strings in C/C++ are concatenated.
	// this is very useful for writing long shader programs inline.

	//look up the locations of vertex attributes:
	Position_vec4 = glGetAttribLocation(program, "Position");
	Normal_vec3 = glGetAttribLocation(program, "Normal");
	Color_vec4 = glGetAttribLocation(program, "Color");
	TexCoord_vec2 = glGetAttribLocation(program, "TexCoord");

	//look up the locations of uniforms:
	OBJECT_TO_CLIP_mat4 = glGetUniformLocation(program, "OBJECT_TO_CLIP");
	OBJECT_TO_LIGHT_mat4x3 = glGetUniformLocation(program, "OBJECT_TO_LIGHT");
	NORMAL_TO_LIGHT_mat3 = glGetUniformLocation(program, "NORMAL_TO_LIGHT");

	ROUGHNESS_float = glGetUniformLocation(program, "ROUGHNESS");

	EYE_vec3 = glGetUniformLocation(program, "EYE");

	LIGHT_TYPE_int = glGetUniformLocation(program, "LIGHT_TYPE");
	LIGHT_LOCATION_vec3 = glGetUniformLocation(program, "LIGHT_LOCATION");
	LIGHT_DIRECTION_vec3 = glGetUniformLocation(program, "LIGHT_DIRECTION");
	LIGHT_ENERGY_vec3 = glGetUniformLocation(program, "LIGHT_ENERGY");
	LIGHT_CUTOFF_float = glGetUniformLocation(program, "LIGHT_CUTOFF");

	GLuint TEX_sampler2D = glGetUniformLocation(program, "TEX");

	//set TEX to always refer to texture binding zero:
	glUseProgram(program); //bind program -- glUniform* calls refer to this program now

	glUniform1i(TEX_sampler2D, 0); //set TEX to sample from GL_TEXTURE0

	glUseProgram(0); //unbind program -- glUniform* calls refer to ??? now
}

OutlineProgram0::~OutlineProgram0() {
	glDeleteProgram(program);
	program = 0;
}

/*
==========================================================================
*/

GLuint empty_vao = 0;

Load< void > init_empty_vao(LoadTagEarly, [](){
	glGenVertexArrays(1, &empty_vao);
});

Load< OutlineProgram1 > OutlineProgram1(LoadTagEarly);

OutlineProgram1::OutlineProgram1() {
	//Compile vertex and fragment shaders using the convenient 'gl_compile_program' helper function:
	program = gl_compile_program(
		//vertex shader:
		"#version 330\n"
		"void main() {\n"
		"	gl_Position = vec4(4 * (gl_VertexID & 1) - 1,  2 * (gl_VertexID & 2) - 1, 0.0, 1.0);\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"uniform sampler2D COLOR_TEX;\n"
		"uniform sampler2D NORMAL_Z_TEX;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
    " vec2 pos = ivec2(gl_FragCoord);\n"
    " vec4 x0 = texelFetch(NORMAL_Z_TEX, ivec2(pos.x - 1, pos.y));\n"
    " vec4 x1 = texelFetch(NORMAL_Z_TEX, ivec2(pos.x + 1, pos.y));\n"
    " vec4 y0 = texelFetch(NORMAL_Z_TEX, ivec2(pos.x, pos.y - 1));\n"
    " vec4 y1 = texelFetch(NORMAL_Z_TEX, ivec2(pos.x, pos.y + 1));\n"
    " vec4 c = vec4(0.0, 0.0, 0.0, 0.0);\n"
    " if (dot(x0.xyz, x1.xyz) > 0.9 &&\n"
    "  dot(y0.xyz, y1.xyz) > 0.9 &&\n"
    "  abs(x0.w - x1.w) < 9.0 &&\n"
    "  abs(y0.w - y1.w) < 9.0) {\n"
    "  c = texelFetch(COLOR_TEX, ivec2(gl_FragCoord), 0);\n"
    " }\n"
		"	fragColor = c;\n"
		"}\n"
	);

	GLuint COLOR_TEX_sampler2D = glGetUniformLocation(program, "COLOR_TEX");

  GLuint NORMAL_Z_TEX_sampler2D = glGetUniformLocation(program, "NORMAL_Z_TEX");

	//set TEX to always refer to texture binding zero:
	glUseProgram(program); //bind program -- glUniform* calls refer to this program now
	glUniform1i(TEX_sampler2D, 0); //set TEX to sample from GL_TEXTURE0
	glUseProgram(0); //unbind program -- glUniform* calls refer to ??? now
}

OutlineProgram1::~OutlineProgram1() {
	glDeleteProgram(program);
	program = 0;
}
