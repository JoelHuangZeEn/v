#include "GameLevel.hpp"
#include "DrawLines.hpp"
#include "Load.hpp"
#include "data_path.hpp"
#include "demo_menu.hpp"
#include "OutlineProgram.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <iostream>

#define PI 3.1415926f

//used for lookup later:
Mesh const *mesh_Goal = nullptr;
//Mesh const *mesh_Body_P1 = nullptr;
//Mesh const *mesh_Body_P2 = nullptr;

//names of mesh-to-collider-mesh:
std::unordered_map< Mesh const *, Mesh const * > mesh_to_collider;

GLuint vao_level = -1U;

Load< MeshBuffer > level1_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer *ret = new MeshBuffer(data_path("level1.pnct"));
	vao_level = ret->make_vao_for_program(outline_program->program);

  //key objects:
  mesh_Goal = &ret->lookup("Goal");
  //mesh_Body_P1 = &ret->lookup("Body1");
  //mesh_Body_P2 = &ret->lookup("Body2");

  //collidable objects:
  mesh_to_collider.insert(std::make_pair(&ret->lookup("Room"), &ret->lookup("Room")));

	return ret;
});

GameLevel::GameLevel(std::string const &scene_file) {
  uint32_t decorations = 0;

  auto load_fn = [this, &decorations](Scene &, Transform *transform, std::string const &mesh_name){
    Mesh const *mesh = &level1_meshes->lookup(mesh_name);

    drawables.emplace_back(transform);
    Drawable::Pipeline &pipeline = drawables.back().pipeline;

    //set up drawable to draw mesh from buffer:
    pipeline = outline_program_pipeline;
    pipeline.vao = vao_level;
    pipeline.type = mesh->type;
    pipeline.start = mesh->start;
    pipeline.count = mesh->count;

    if (transform->name.substr(0, 4) == "Move") {

      std::cout << "Movable detected: " << transform->name << std::endl;

      movable_data.emplace_back(transform);
      Movable &data = movable_data.back();

      auto f = mesh_to_collider.find(mesh);
      mesh_colliders.emplace_back(transform, *f->second, *level1_meshes, &data);
      std::cout << mesh_colliders.back().movable << std::endl;

    } else if (transform->name.substr(0, 4) == "Goal") {
      goals.emplace_back(transform);
    } else if (transform->name.substr(0, 5) == "Body1") {
      body_P1_transform = transform;
    } else if (transform->name.substr(0, 5) == "Body2") {
      body_P2_transform = transform;
    } else {
      auto f = mesh_to_collider.find(mesh);
      if (f != mesh_to_collider.end()) {
        mesh_colliders.emplace_back(transform, *f->second, *level1_meshes);
      } else {
        decorations++;
      }
    }

    pipeline.set_uniforms = [](){
      glUniform1f(outline_program->ROUGHNESS_float, 1.0f);
    };
  };
	//Load scene (using Scene::load function), building proper associations as needed:
	load(scene_file, load_fn);

  // Build the mapping between movables and orthographic cameras
  for (auto &m : movable_data) {
    std::string &xf_name = m.transform->name;
    for (auto &oc : orthocams) {
      if (oc.transform->name.substr(0, xf_name.size()) == xf_name) {
        m.init_cam(&oc);
        break;
      }
    }
  }

  // Get the player cameras
  for (auto &c : cameras) {
    if (c.transform->name.substr(0, 7) == "Player1") {
      cam_P1 = &c;
    } else if (c.transform->name.substr(0, 7) == "Player2") {
      cam_P2 = &c;
    }
  }

}

GameLevel::~GameLevel() {

}

void GameLevel::reset() {
  for (Movable &m : movable_data) {
    m.offset = 0.0f;
    m.update();
  }
}

//Helper: maintain a framebuffer to hold rendered geometry
struct FB {
	//object data gets stored in these textures:
	GLuint normal_z_tex = 0;

	//output image gets written to this texture:
	GLuint output_tex = 0;

	//depth buffer is shared between objects + lights pass:
	GLuint depth_rb = 0;

	GLuint fb0 = 0; //(output + normal & z) + depth

	glm::uvec2 size = glm::uvec2(0);

	void resize(glm::uvec2 const &drawable_size) {
		if (drawable_size == size) return;
		size = drawable_size;

		//helper to allocate a texture:
		auto alloc_tex = [&] (GLuint &tex, GLenum internal_format) {
			if (tex == 0) glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, internal_format, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindTexture(GL_TEXTURE_2D, 0);
		};

    auto alloc_recttex = [&] (GLuint &tex, GLenum internal_format) {
      if (tex == 0) glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_RECTANGLE, tex);
      glTexImage2D(GL_TEXTURE_RECTANGLE, 0, internal_format, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    };

		//set up normal_roughness_tex as a 16-bit floating point RGBA texture:
		alloc_recttex(normal_roughness_tex, GL_RGBA16F);

		//set up output_tex as an 8-bit fixed point RGBA texture:
		alloc_recttex(output_tex, GL_RGBA8);

		//if depth_rb does not have a name, name it:
		if (depth_rb == 0) glGenRenderbuffers(1, &depth_rb);
		//set up depth_rb as a 24-bit fixed-point depth buffer:
		glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size.x, size.y);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		//if objects framebuffer doesn't have a name, name it and attach textures:
		if (fb0 == 0) {
			glGenFramebuffers(1, &fb0);
			//set up framebuffer: (don't need to do when resizing)
			glBindFramebuffer(GL_FRAMEBUFFER, fb0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_tex, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normal_z_tex, 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
			GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
			glDrawBuffers(2, bufs);
			check_fb();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}
} fb;

void GameLevel::draw( Camera const &camera ) {
  draw( camera, camera.make_projection() * camera.transform->make_world_to_local() );
}

void GameLevel::draw( Camera const &camera, glm::mat4 world_to_clip) {

  fb.resize(drawable_size);
  
	//--- actual drawing ---
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glm::vec3 eye = camera.transform->make_local_to_world()[3];

	for (auto const &light : lights) {
		glm::mat4 light_to_world = light.transform->make_local_to_world();
		//set up lighting information for this light:
		glUseProgram(outline_program_0->program);
		glUniform3fv(outline_program_0->EYE_vec3, 1, glm::value_ptr(eye));
		glUniform3fv(outline_program_0->LIGHT_LOCATION_vec3, 1, glm::value_ptr(glm::vec3(light_to_world[3])));
		glUniform3fv(outline_program_0->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(-light_to_world[2])));
		glUniform3fv(outline_program_0->LIGHT_ENERGY_vec3, 1, glm::value_ptr(light.energy));
		if (light.type == Scene::Light::Point) {
			glUniform1i(outline_program_0->LIGHT_TYPE_int, 0);
			glUniform1f(outline_program_0->LIGHT_CUTOFF_float, 1.0f);
		} else if (light.type == Scene::Light::Hemisphere) {
			glUniform1i(outline_program_0->LIGHT_TYPE_int, 1);
			glUniform1f(outline_program_0->LIGHT_CUTOFF_float, 1.0f);
		} else if (light.type == Scene::Light::Spot) {
			glUniform1i(outline_program_0->LIGHT_TYPE_int, 2);
			glUniform1f(outline_program_0->LIGHT_CUTOFF_float, std::cos(0.5f * light.spot_fov));
		} else if (light.type == Scene::Light::Directional) {
			glUniform1i(outline_program_0->LIGHT_TYPE_int, 3);
			glUniform1f(outline_program_0->LIGHT_CUTOFF_float, 1.0f);
		}
		Scene::draw(world_to_clip);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glBlendEquation(GL_FUNC_ADD);
	}
}

GameLevel::Movable::Movable(Transform *transform_) : transform(transform_) {

  init_pos = transform->position;

}

void GameLevel::Movable::init_cam(OrthoCam *cam) {
  cam_flat = cam;

  // Cameras are directed along the -z axis. Get the transformed z-axis.
  axis = cam->transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
  if (axis != glm::vec3(0.0f)) axis = glm::normalize(axis);

  std::cout << "Axis: " << axis.x << "\t" << axis.y << "\t" << axis.z << std::endl;

  // Get the transformed origin;
  mover_pos = cam->transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  std::cout << "Position: " << mover_pos.x << "\t" << mover_pos.y << "\t" << mover_pos.z << std::endl;

}

void GameLevel::Movable::update() {

  if (player) {
    glm::vec3 start = transform->position;
    glm::vec3 end = init_pos + offset * axis;
    //glm::vec4 move = glm::vec4(end - start, 1.0f);
    //player->position += glm::vec3(player->make_world_to_local() * move);
    player->position += end - start;
    transform->position = end;
  }
  else {
    transform->position = init_pos + offset * axis;
  }

}

GameLevel::Movable *GameLevel::movable_get(Transform *transform) {

  glm::vec3 pos = transform->make_local_to_world()[3]; // * (0,0,0,1)
  glm::vec3 axis = transform->make_local_to_world() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
  if (axis == glm::vec3(0.0f)) return nullptr;
  axis = glm::normalize(axis);

  std::cout << "Pos: " << pos.x << "\t" << pos.y << "\t" << pos.z << std::endl;
  std::cout << "Axis: " << axis.x << "\t" << axis.y << "\t" << axis.z << std::endl;

  for (Movable &m : movable_data) {
    std::cout << "Mover: " << m.mover_pos.x << "\t" << m.mover_pos.y << "\t" << m.mover_pos.z << std::endl;

    glm::vec3 dist = m.mover_pos - pos;
    std::cout << glm::dot( dist, dist ) << std::endl;
    if (glm::dot(dist, dist) <= m.pos_tolerance * m.pos_tolerance) {
      std::cout << "Position within threshhold. Checking axis..." << std::endl;
      std::cout << "Dot: " << glm::dot(axis, m.axis) << std::endl;
      if (glm::dot(axis, m.axis) > m.axis_tolerance) {
        return &m;
      }
    }
  }

  return nullptr;

}
