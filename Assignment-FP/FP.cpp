// Karl Yerkes
// 2022-01-20

#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"
#include "al_ext/statedistribution/al_CuttleboneDomain.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"

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
  Parameter pointSize{"/pointSize", "", 4.5, 1.0, 10.0};
  Parameter timeStep{"/timeStep", "", 0.3, 0.01, 0.6};
  Parameter dragFactor{"/dragFactor", "", 0.7, 0.0, 0.9};
  Parameter springStiffness{"Spring Stiffness", 0.1, 0.0, 0.9};
  Parameter springLength{"Spring Length", 6, 0, 50};
  //Parameter repulsivity{"Repulsivity", 1, 0, 2};

  Parameter smoothingRadius{"Smoothing Radius", 3.2, 0.5, 5.0}; // Controls the connection length!
  Parameter gasStiffness{"Gas Stiffness", 42.0, 1.0, 100.0};    // Replaces Repulsivity
  Parameter restDensity{"Rest Density", 5.5, 1.0, 20.0};        // Fluid crowding
  Parameter viscosity{"Viscosity", 1.4, 0.0, 5.0};

  Parameter enableWarp{"Enable Warp", 1.0, 0.0, 1.0};


  ShaderProgram pointShader;

  //  simulation state
  Mesh mesh;  // position *is inside the mesh* mesh.vertices() are the positions
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
        gui.add(pointSize);  // add parameter to GUI
        gui.add(timeStep);   // add parameter to GUI
        gui.add(dragFactor);   // add parameter to GUI
        gui.add(springStiffness);
        gui.add(springLength);
       // gui.add(repulsivity);
        gui.add(smoothingRadius);
        gui.add(gasStiffness);
        gui.add(restDensity);
        gui.add(viscosity);
        gui.add(enableWarp);
    }
  }

  void onCreate() override {
    // compile shaders
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));

    // set initial conditions of the simulation

    auto randomColor = []() { return HSV(rnd::uniform(), 1.0f, 1.0f); };

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
    Vec3f ux(nav().ux());
    Vec3f uy(nav().uy());
    Vec3f uf(nav().uf());

    // calculate spring force between this particle and the camera position focal point.
Vec3f focal_point = camPos + (uf * 13.0f); // Center of the fluid container
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

    //SPH
    float h = smoothingRadius;             // Smoothing Length (Radius of influence)
    float rest_density = restDensity;  // Target Density (rho_0)
    float k_gas = gasStiffness;        // Stiffness/Pressure multiplier
    float mu = viscosity;            // Viscosity (thickness)

    float h2 = h * h;
    float poly6 = 315.0f / (64.0f * 3.14159265f * pow(h, 9));
    float spiky_grad = -45.0f / (3.14159265f * pow(h, 6));
    float visc_lap = 45.0f / (3.14159265f * pow(h, 6));

    std::vector<float> density(mesh.vertices().size(), 0.0f);
    std::vector<float> pressure(mesh.vertices().size(), 0.0f);
// PASS 1: Calculate Density & Pressure
    for (int i = 0; i < mesh.vertices().size(); ++i) {
      for (int j = 0; j < mesh.vertices().size(); ++j) {
        Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
        float r2 = dir.mag() * dir.mag();

        if (r2 < h2) {
          density[i] += mass[j] * poly6 * pow(h2 - r2, 3);
        }
      }
      pressure[i] = k_gas * (density[i] - rest_density);
    }

    // PASS 2: Calculate Pressure and Viscosity Forces
    for (int i = 0; i < mesh.vertices().size(); ++i) {
      for (int j = i + 1; j < mesh.vertices().size(); ++j) {
        
        Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
        float r = dir.mag();

        if (r < h && r > 0.0001f) {
          dir /= r; // Normalize direction

          float p_term = (pressure[i] + pressure[j]) / 2.0f;
          float kernel_grad = spiky_grad * (h - r) * (h - r);
          float f_press_mag = -p_term * kernel_grad; 

          Vec3f v_diff = velocity[j] - velocity[i];
          float kernel_lap = visc_lap * (h - r);
          
          // Safety check: prevent division by zero
          float safe_density = (density[j] > 0.0001f) ? density[j] : 0.0001f;
          Vec3f f_visc = v_diff * (mu * mass[j] * kernel_lap / safe_density);

          Vec3f total_force = (dir * f_press_mag) + f_visc;
          
          force[i] += total_force;
          force[j] -= total_force; 
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
      if (enableWarp.get() > 0.05f){
          Vec3f relPos = position[i] - camPos;
          float relX = relPos.dot(ux);
          float relY = relPos.dot(uy);
          float relZ = relPos.dot(uf); 

          if (relZ > 0.001f) {
            // Replaced M_PI to guarantee MSVC compilation
            float edge = relZ * tan((lens().fovy() / 2.0f) * (3.14159265f / 180.0f));
            float x_edge = edge * (float(width()) / height());

            // Warp X and Y
            if (relX > x_edge) position[i] -= ux * (x_edge * 2 - 0.05f);
            if (relX < -x_edge) position[i] += ux * (x_edge * 2 - 0.05f);
            if (relY > edge) position[i] -= uy * (edge * 2 - 0.05f);
            if (relY < -edge) position[i] += uy * (edge * 2 - 0.05f);
          }

            // Z-Axis Limits (Keep them in the viewing plane)
            // if (relZ < 6.0f) position[i] += uf * 0.1f; 
            // if (relZ > 20.0f) position[i] -= uf * 0.1f; 
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

    if (!isPrimary()) {
      for (int i = 0; i < N; i++) {
        mesh.vertices()[i] = state().position[i];
      }
    }


    // 1. DRAW RIBBONS 
    Mesh ribbons;
    ribbons.primitive(Mesh::TRIANGLES);

    float h = smoothingRadius; // Link visuals directly to the physics slider!
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

    g.draw(ribbons); // Successfully draws the blue fluid connections!
    g.blendAdd();

    // 2. DRAW PARTICLES SECOND
    g.shader(pointShader);
    g.shader().uniform("pointSize", pointSize / 100);
    g.draw(mesh);
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