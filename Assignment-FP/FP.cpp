// Karl Yerkes
// 2022-01-20

#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/math/al_Random.hpp"
#include "al_ext/statedistribution/al_CuttleboneDomain.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"
#include "al/graphics/al_Shapes.hpp"

using namespace al;

#include <algorithm>
#include <fstream>
#include <random>
#include <vector>

using namespace std;

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}

string slurp(string fileName);  // forward declaration

const int N = 300;

struct WorldState {
  double time;
  int frame;
  Pose camera;
  Vec3f position[N]; 
  Color color[N];
};

struct AlloApp : DistributedAppWithState<WorldState> {
  //Define Controllable Variable Here.
  //point
  Parameter pointSize{"Point Size", "", 4.5, 1.0, 10.0};
  Parameter timeStep{"Time Step", "", 0.3, 0.01, 0.6};
  Parameter dragFactor{"Drag Factor", "", 0.7, 0.0, 0.9};

  //Spring behavior
  Parameter springStiffness {"Spring Stiffness", 0.1, 0.0, 0.9};
  Parameter springLength    {"Spring Length", 6, 0, 50};
  Parameter focalDepth      {"Focal Depth", 75.0, 5.0, 100.0};

  //swarm Behavior Parameter
  Parameter perceptionRadius    {"Perception Radius", 3.2, 0.5, 20.0}; 
  Parameter repulsionStrength   {"Repulsion Strength", 42.0, 1.0, 100.0};    
  Parameter desiredPersonalSpace{"Personal Space", 5.5, 1.0, 20.0};     
  Parameter viscosity           {"Viscosity", 1.4, 0.0, 10.0};


  //bubbles
  Parameter bubbleSize          {"Bubble Size", 1.5, 0.1, 5.0};
  

  ParameterBool enableSpring  {"Enable Spring", "", 1.0f};
  ParameterBool springToOrigin{"Spring Center to Origin", "", 0.0f};
  ParameterBool enableSwarm   {"Enable Swarm", "", 1.0f};
  ParameterBool enableWarp    {"Enable Warp", "", 1.0f};
  ParameterBool enableRibbons {"Enable Ribbons", "", 1.0f};
  ParameterBool enableBubbles {"Enable Bubbles", "", 1.0f};

  ShaderProgram pointShader;
  ShaderProgram bubbleShader;

  //  simulation state
  Mesh mesh;
  Mesh sphereMesh;
  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;


  void onInit() override {
    // auto cuttleboneDomain =
    //     CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    // if (!cuttleboneDomain) {
    //   std::cerr << "ERROR: Could not start Cuttlebone. Quitting." << std::endl;
    //   quit();
    // }

    if (isPrimary()) {
    // set up GUI
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();

        //points
        gui.add(pointSize); 
        gui.add(timeStep);   
        gui.add(dragFactor);   

        //Springs
        gui.add(springStiffness);
        gui.add(springLength);
        gui.add(focalDepth);
        
        //Swarm 
        gui.add(perceptionRadius);
        gui.add(repulsionStrength);
        gui.add(desiredPersonalSpace);
        gui.add(viscosity);
        gui.add(bubbleSize);

        //Toggles
        gui.add(enableSpring);
        gui.add(springToOrigin);
        gui.add(enableSwarm);
        gui.add(enableWarp);
        gui.add(enableRibbons);
        gui.add(enableBubbles);
    }
  }

  void onCreate() override {
    // compile shaders
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));
    bubbleShader.compile(slurp("../bubble-vertex.glsl"),
                        slurp("../bubble-fragment.glsl"));

    addSphere(sphereMesh, 1.0, 16, 16);
    sphereMesh.generateNormals(); 
                        
    // set initial conditions of the simulation
    // auto randomColor = []() { return HSV(rnd::uniform(), 1.0f, 1.0f); };

    mesh.primitive(Mesh::POINTS);

    for (int _ = 0; _ < N; _++) {
      mesh.vertex(randomVec3f(5));
      mesh.color(1.0, 1.0, 1.0);  // white

      // float m = rnd::uniform(3.0, 0.5);
      float m = 3 + rnd::normal() / 2;
      if (m < 0.5) m = 0.5;
      mass.push_back(m);

      // using a simplified volume/size relationship
      mesh.texCoord(pow(m, 1.0f / 3), 0);  // s, t

      // separate state arrays
      velocity.push_back(randomVec3f(0.1));
      force.push_back(randomVec3f(1));
    }
    
    for(int i = 0; i< mesh.vertices().size(); i++){
      mesh.vertices()[i].normalize();
    }

    nav().pos(0, 0, 10);
  }
  bool freeze = false;


  void onAnimate(double dt) override {
    if (!isPrimary()) {
      return;
    }

    state().frame++;
    state().time += dt;

    if (freeze) return;


    //Camera position
    Vec3f camPos(nav().pos());
    Vec3f ur(nav().ur()); //unit up axis aligned vs not aligned
    Vec3f uu(nav().uu()); // unint right
    Vec3f uf(nav().uf());

    // 1. Spring module
    
    if (enableSpring.get() == 1.0f) {
      Vec3f focal_point;

      if (springToOrigin.get() == 1.0f) {
          focal_point = Vec3f(0.0f, 0.0f, 0.0f); // Anchor to the center of the world
      } else {
          focal_point = camPos + (uf * focalDepth.get());  // calculate spring force between this particle and the camera position focal point.
      }


      for (int i = 0; i < velocity.size(); i++) {
        auto& me = mesh.vertices()[i];
        Vec3f dir = focal_point - me;
        float dist = dir.mag(); 
        float force_amount = (dist - springLength) * springStiffness;
        
        if (dist > 0.0001f) {
          dir /= dist; // normalize
          force[i] += dir * force_amount;
        }
      }
    }

    // Calculate repulsive forces....
    //  for (int i = 0; i < mesh.vertices().size(); ++i) {
    //   for (int j = i + 1; j < mesh.vertices().size(); ++j) {
    //     // what is the *direction* of the force? (with magnitude of 1)

    //     Vec3f force_direction = (mesh.vertices()[i] - mesh.vertices()[j]);
    //     float distance = force_direction.mag(); // aka "r"
    //     force_direction /= distance; // the same as .normalize() in this case

    //     float force_amount = repulsivity * mass[i] * mass[j] / (distance * distance);

    //     // i and j are a pair
    //     // limit large forces... if the force is too large, ignore it

    //     if (force_amount > 10) {
    //       force_amount = 10;
    //     }

    //     force[i] += force_direction * force_amount;
    //     force[j] -= force_direction * force_amount;
    //   }
    // }

    if(enableSwarm.get() == 1.0f){
    float radius = perceptionRadius.get();              // Smoothing Length (Radius of influence)
    float target_space = desiredPersonalSpace.get();    // Target Density (rho_0)
    float repulsion_factor = repulsionStrength.get();   // Stiffness/Pressure multiplier
    float friction_factor = viscosity.get();            // Viscosity (thickness)

    float radius2 = radius * radius;
    
    float proximity_weight    = 315.0f / (64.0f * 3.14159265f * pow(radius, 9));
    float repulsion_gradient  = -45.0f / (3.14159265f * pow(radius, 6));
    float alignment_weight    = 45.0f / (3.14159265f * pow(radius, 6));

    std::vector<float> crowdedness(mesh.vertices().size(), 0.0f);
    std::vector<float> claustrophobia(mesh.vertices().size(), 0.0f);

// PASS 1: Calculate Density & Pressure
    for (int i = 0; i < mesh.vertices().size(); ++i) {
      for (int j = 0; j < mesh.vertices().size(); ++j) {
        Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
        float r2 = dir.mag() * dir.mag();

        if (r2 < radius2) {
          crowdedness[i] += mass[j] * proximity_weight * pow(radius2 - r2, 3);
        }
      }
      claustrophobia[i] = repulsion_factor * (crowdedness[i] - target_space);
    }

    // PASS 2: Calculate Pressure and Viscosity Forces
    for (int i = 0; i < mesh.vertices().size(); ++i) {
      for (int j = i + 1; j < mesh.vertices().size(); ++j) {
        
        Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
        float r = dir.mag();

        if (r < radius && r > 0.0001f) {
          dir /= r; // Normalize direction

          float shared_claustrophobia = (claustrophobia[i] + claustrophobia[j]) / 2.0f;
          float kernel_grad = repulsion_gradient * (radius - r) * (radius - r);
          float push_mag = -shared_claustrophobia * kernel_grad; 

          Vec3f v_diff = velocity[j] - velocity[i];
          float kernel_lap = alignment_weight * (radius - r);
          
          // Safety check: prevent division by zero
          float safe_crowding = (crowdedness[j] > 0.0001f) ? crowdedness[j] : 0.0001f;
          Vec3f alignment_force = v_diff * (friction_factor * mass[j] * kernel_lap / safe_crowding);
        
          Vec3f total_force = (dir * push_mag) + alignment_force;
          
          force[i] += total_force;
          force[j] -= total_force; 
        }
      }
    }
  }
  //viscous drag
  for (int i = 0; i < velocity.size(); i++) {
    force[i] += - velocity[i] * dragFactor; // F = -bv
  }

    // Numerical Integration
    vector<Vec3f> &position(mesh.vertices());
    for (int i = 0; i < velocity.size(); i++) {
      // "semi-implicit" Euler integration
      velocity[i] += force[i] / mass[i] * timeStep;
      position[i] += velocity[i] * timeStep;

    float normalized_y = (position[i].y + 5.0f) / 10.0f;
    normalized_y = std::max(0.0f, std::min(normalized_y, 1.0f));
    state().color[i] = Color(normalized_y * 0.5f, normalized_y * 0.8f + 0.2f, 1.0f, 1.0f);

      if (enableWarp.get() == 1.0f){
          Vec3f relPos = position[i] - camPos;
          float relX = relPos.dot(ur); 
          float relY = relPos.dot(uu); 
          float relZ = relPos.dot(uf); 

          if (relZ > 0.001f) {
            float edge = relZ * tan((lens().fovy() / 2.0f) * (3.14159265f / 180.0f));
            float x_edge = edge * (float(width()) / height());

            // Warp X and Y
            if (relX > x_edge) position[i] -= ur * (x_edge * 2 - 0.05f);
            if (relX < -x_edge) position[i] += ur * (x_edge * 2 - 0.05f);
            if (relY > edge) position[i] -= uu * (edge * 2 - 0.05f);
            if (relY < -edge) position[i] += uu * (edge * 2 - 0.05f);
          }

            float currentDepth = focalDepth.get();
            if (relZ < currentDepth - 15.0f) position[i] += uf * 0.6f; 
            if (relZ > currentDepth + 15.0f) position[i] -= uf * 0.6f;
      }
          

  }
    // 5. Cleanup & Network Sync
    for (auto &a : force) a.set(0); // clear all accelerations (IMPORTANT!!)
    for (int i = 0; i < N; i++) state().position[i] = mesh.vertices()[i];
    state().camera.set(nav());  
  }

  bool onKeyDown(const Keyboard& k) override {
    if (k.key() == ' ') {
      freeze = !freeze;
    }

    if (k.key() == '1') {
      // introduce some "random" forces
      for (int i = 0; i < velocity.size(); i++) {
        // F = ma
        force[i] += randomVec3f(1);
      }
    }

    if (k.key() == 'r') {
      // reset positions and velocities
      for (int i = 0; i < velocity.size(); i++) {
        mesh.vertices()[i] = randomVec3f(5);
        velocity[i] = randomVec3f(0.1);
        force[i] = randomVec3f(1);
      }
    }
 
    return true;
  }

void onDraw(Graphics& g) override {
    if (!isPrimary()) {
      nav().set(state().camera);
    }
 
    g.clear(0.0);
    g.blending(true);
   // g.blendAdd(); 
    g.blendTrans();
    g.depthTesting(false); // Turn off depth testing so transparent layers stack properly

    // if (!isPrimary()) {
    //   for (int i = 0; i < N; i++) {
    //     mesh.vertices()[i] = state().position[i];
    //   }
    for (int i = 0; i < N; i++) {
        if (!isPrimary()) mesh.vertices()[i] = state().position[i];
        mesh.colors()[i] = state().color[i];
        float normalized_y = (mesh.vertices()[i].y + 5.0f) / 10.0f;
        normalized_y = std::max(0.0f, std::min(normalized_y, 1.0f));
        mesh.colors()[i] = Color(normalized_y * 0.5f, normalized_y * 0.8f + 0.2f, 1.0f, 1.0f);
    }

    g.shader().use(0); 
    g.meshColor();


    // 1. DRAW RIBBONS 
    if (enableRibbons.get() == 1.0f) {
    Mesh ribbons;
    ribbons.primitive(Mesh::TRIANGLES);

    float h = perceptionRadius.get(); // Link visuals directly to the physics slider!
    float line_thickness = 0.02f; 

    for (int i = 0; i < N; i++) {
      for (int j = i + 1; j < N; j++) { 
        Vec3f A = mesh.vertices()[i];
        Vec3f B = mesh.vertices()[j];
        
        Vec3f dir = B - A;
        float distance = dir.mag();

        if (distance < h && distance > 0.0001f) {
          dir /= distance; 
          
          Vec3f view_dir = (nav().pos() - A).normalize();
          Vec3f right = cross(dir, view_dir);
          if (right.mag() < 0.001f) right = Vec3f(1, 0, 0); 
          right.normalize();

          Vec3f offset = right * line_thickness;

          Vec3f v0 = A + offset;
          Vec3f v1 = A - offset;
          Vec3f v2 = B + offset;
          Vec3f v3 = B - offset;
          
          // Normalized distance (0.0 = touching, 1.0 = disconnecting)
          float t = distance / h;
          float math_opacity = 1.0f - t; 
          
          // GRADIENT MATH: Green -> Yellow -> Red
          float r_color = std::min(2.0f * t, 1.0f);
          float g_color = std::min(2.0f * (1.0f - t), 1.0f);
          float b_color = 0.0f;
    
          Color c(r_color, g_color, b_color, math_opacity);
          
          ribbons.vertex(v0); ribbons.color(c);
          ribbons.vertex(v1); ribbons.color(c);
          ribbons.vertex(v2); ribbons.color(c);
          
          ribbons.vertex(v2); ribbons.color(c);
          ribbons.vertex(v1); ribbons.color(c);
          ribbons.vertex(v3); ribbons.color(c);
        }
      }
    }
  

    g.draw(ribbons); 
    g.blendAdd();
  }
    // 2. DRAW PARTICLES SECOND
if (enableBubbles.get() == 1.0f) {
      // BUBBLE MODE (The Wojtan Optical Illusion)
      g.shader().use(0); 
      g.blending(true);
      g.blendTrans();    
      g.depthTesting(true); 

      float b_size = bubbleSize.get();
      
      for (int i = 0; i < N; i++) {
        g.shader(bubbleShader);
        g.pushMatrix();
        g.translate(mesh.vertices()[i]);
        g.scale(b_size);
        g.cullFace(false);
        
        
        Color c = mesh.colors()[i];
        g.color(c.r, c.g, c.b, 0.3f); 
        
        g.draw(sphereMesh); // Render the actual 3D template sphere
        g.popMatrix();
      }

    } else {
      // ORIGINAL POINT MODE
      g.blending(true);
      g.blendAdd();
      g.depthTesting(false);
      g.shader(pointShader);
      g.shader().uniform("pointSize", pointSize / 100);
      g.draw(mesh);
    }
  }
};

int main() {
  AlloApp app;
  app.configureAudio(48000, 512, 2, 0);
  app.start();
}

string slurp(string fileName) {
  fstream file(fileName);
  string returnValue = "";
  while (file.good()) {
    string line;
    getline(file, line);
    returnValue += line + "\n";
  }
  return returnValue;
}