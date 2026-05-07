// Karl Yerkes
// 2022-01-20

#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/math/al_Random.hpp"
#include "al_ext/statedistribution/al_CuttleboneDomain.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"
#include "al/graphics/al_Shapes.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <vector>
#include <unordered_map>

using namespace al;
using namespace std;

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}

string slurp(string fileName);  // forward declaration

const int N = 300;

// =========================================================
// SPATIAL HASH GRID (Keeps the ribbons running at 60 FPS!)
// =========================================================
struct SpatialHash {
  float cellSize;
  std::unordered_map<unsigned long long, std::vector<int>> cells;

  void build(const std::vector<Vec3f>& positions, float radius) {
    cellSize = radius;
    cells.clear();
    for (int i = 0; i < positions.size(); ++i) {
      unsigned long long hash = getHash(positions[i]);
      cells[hash].push_back(i);
    }
  }

  unsigned long long getHash(const Vec3f& pos) {
    int x = std::floor(pos.x / cellSize);
    int y = std::floor(pos.y / cellSize);
    int z = std::floor(pos.z / cellSize);
    
    unsigned long long h1 = 73856093ULL;
    unsigned long long h2 = 19349663ULL;
    unsigned long long h3 = 83492791ULL;
    
    return (x * h1) ^ (y * h2) ^ (z * h3);
  }

  std::vector<int> getNeighbors(const Vec3f& pos) {
    std::vector<int> neighbors;
    int cx = std::floor(pos.x / cellSize);
    int cy = std::floor(pos.y / cellSize);
    int cz = std::floor(pos.z / cellSize);

    for (int z = cz - 1; z <= cz + 1; z++) {
      for (int y = cy - 1; y <= cy + 1; y++) {
        for (int x = cx - 1; x <= cx + 1; x++) {
          unsigned long long hash = (x * 73856093ULL) ^ (y * 19349663ULL) ^ (z * 83492791ULL);
          auto it = cells.find(hash);
          if (it != cells.end()) {
            for (int idx : it->second) {
              neighbors.push_back(idx);
            }
          }
        }
      }
    }
    return neighbors;
  }
};


struct WorldState {
  double time;
  int frame;
  Pose camera;
  Vec3f position[N]; 
  Color color[N];
};

struct AlloApp : DistributedAppWithState<WorldState> {
  // GUI Parameters
  Parameter pointSize{"Point Size", "", 4.5, 1.0, 10.0};
  Parameter timeStep{"Time Step", "", 0.3, 0.01, 0.6};
  Parameter dragFactor{"Drag Factor", "", 0.7, 0.0, 0.9};

  Parameter springStiffness {"Spring Stiffness", 0.1, 0.0, 0.9};
  Parameter springLength    {"Spring Length", 6, 0, 50};
  Parameter focalDepth      {"Focal Depth", 75.0, 5.0, 100.0};

  Parameter perceptionRadius    {"Perception Radius", 3.2, 0.5, 20.0}; 
  Parameter repulsionStrength   {"Repulsion Strength", 42.0, 1.0, 100.0};    
  Parameter desiredPersonalSpace{"Personal Space", 5.5, 1.0, 20.0};     
  Parameter viscosity           {"Viscosity", 1.4, 0.0, 10.0};

  Parameter bubbleSize          {"Bubble Size", 1.5, 0.1, 5.0};
  
  ParameterBool enableSpring  {"Enable Spring", "", 1.0f};
  ParameterBool springToOrigin{"Spring Center to Origin", "", 0.0f};
  ParameterBool enableSwarm   {"Enable Swarm", "", 1.0f};
  ParameterBool enableWarp    {"Enable Warp", "", 1.0f};
  ParameterBool enableRibbons {"Enable Ribbons", "", 1.0f};
  ParameterBool enableBubbles {"Enable Bubbles", "", 1.0f};

  ShaderProgram pointShader;
  ShaderProgram bubbleShader; 

  Mesh mesh;
  Mesh sphereMesh; 
  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;


  void onInit() override {
    if (isPrimary()) {
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();

        gui.add(pointSize); 
        gui.add(timeStep);   
        gui.add(dragFactor);   

        gui.add(springStiffness);
        gui.add(springLength);
        gui.add(focalDepth);
        
        gui.add(perceptionRadius);
        gui.add(repulsionStrength);
        gui.add(desiredPersonalSpace);
        gui.add(viscosity);
        gui.add(bubbleSize);

        gui.add(enableSpring);
        gui.add(springToOrigin);
        gui.add(enableSwarm);
        gui.add(enableWarp);
        gui.add(enableRibbons);
        gui.add(enableBubbles);
    }
  }

  void onCreate() override {
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));
                        
    bubbleShader.compile(slurp("../bubble-vertex.glsl"),
                         slurp("../bubble-fragment.glsl"));

    addSphere(sphereMesh, 1.0, 16, 16);
    sphereMesh.generateNormals(); 
                        
    mesh.primitive(Mesh::POINTS);

    for (int _ = 0; _ < N; _++) {
      mesh.vertex(randomVec3f(5));
      mesh.color(1.0, 1.0, 1.0);  

      float m = 3 + rnd::normal() / 2;
      if (m < 0.5) m = 0.5;
      mass.push_back(m);

      mesh.texCoord(pow(m, 1.0f / 3), 0);  

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
    if (!isPrimary()) return;
    
    state().frame++;
    state().time += dt;

    if (freeze) return;

    Vec3f camPos(nav().pos());
    Vec3f ur(nav().ur()); 
    Vec3f uu(nav().uu()); 
    Vec3f uf(nav().uf());

    // 1. Spring module
    if (enableSpring.get() == 1.0f) {
      Vec3f focal_point;
      if (springToOrigin.get() == 1.0f) focal_point = Vec3f(0.0f, 0.0f, 0.0f); 
      else focal_point = camPos + (uf * focalDepth.get());  

      for (int i = 0; i < velocity.size(); i++) {
        auto& me = mesh.vertices()[i];
        Vec3f dir = focal_point - me;
        float dist = dir.mag(); 
        float force_amount = (dist - springLength.get()) * springStiffness.get();
        if (dist > 0.0001f) {
          dir /= dist; 
          force[i] += dir * force_amount;
        }
      }
    }

    // 2. Swarm Physics
    if(enableSwarm.get() == 1.0f){
      float radius = perceptionRadius.get();              
      float target_space = desiredPersonalSpace.get();    
      float repulsion_factor = repulsionStrength.get();   
      float friction_factor = viscosity.get();            
      float radius2 = radius * radius;
      
      float proximity_weight    = 315.0f / (64.0f * 3.14159265f * pow(radius, 9));
      float repulsion_gradient  = -45.0f / (3.14159265f * pow(radius, 6));
      float alignment_weight    = 45.0f / (3.14159265f * pow(radius, 6));

      std::vector<float> crowdedness(mesh.vertices().size(), 0.0f);
      std::vector<float> claustrophobia(mesh.vertices().size(), 0.0f);

      SpatialHash physicsGrid;
      physicsGrid.build(mesh.vertices(), radius);

      // PASS 1: Density
      for (int i = 0; i < mesh.vertices().size(); ++i) {
        auto neighbors = physicsGrid.getNeighbors(mesh.vertices()[i]);
        for (int j : neighbors) {
          Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
          float r2 = dir.mag() * dir.mag();
          if (r2 < radius2) {
            crowdedness[i] += mass[j] * proximity_weight * pow(radius2 - r2, 3);
          }
        }
        claustrophobia[i] = repulsion_factor * (crowdedness[i] - target_space);
      }

      // PASS 2: Forces
      for (int i = 0; i < mesh.vertices().size(); ++i) {
        auto neighbors = physicsGrid.getNeighbors(mesh.vertices()[i]);
        for (int j : neighbors) {
          if (j <= i) continue; 
          
          Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
          float r = dir.mag();

          if (r < radius && r > 0.0001f) {
            dir /= r; 

            float shared_claustrophobia = (claustrophobia[i] + claustrophobia[j]) / 2.0f;
            float kernel_grad = repulsion_gradient * (radius - r) * (radius - r);
            float push_mag = -shared_claustrophobia * kernel_grad; 

            Vec3f v_diff = velocity[j] - velocity[i];
            float kernel_lap = alignment_weight * (radius - r);
            float safe_crowding = (crowdedness[j] > 0.0001f) ? crowdedness[j] : 0.0001f;
            Vec3f alignment_force = v_diff * (friction_factor * mass[j] * kernel_lap / safe_crowding);
          
            Vec3f total_force = (dir * push_mag) + alignment_force;
            force[i] += total_force;
            force[j] -= total_force; 
          }
        }
      }
    }
    
    for (int i = 0; i < velocity.size(); i++) force[i] += - velocity[i] * dragFactor.get(); 

    // Numerical Integration
    vector<Vec3f> &position(mesh.vertices());
    for (int i = 0; i < velocity.size(); i++) {
      velocity[i] += force[i] / mass[i] * timeStep.get();
      position[i] += velocity[i] * timeStep.get();

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
    
    for (auto &a : force) a.set(0); 
    for (int i = 0; i < N; i++) state().position[i] = mesh.vertices()[i];
    state().camera.set(nav());  
  }

  bool onKeyDown(const Keyboard& k) override {
    if (k.key() == ' ') freeze = !freeze;
    if (k.key() == '1') {
      for (int i = 0; i < velocity.size(); i++) force[i] += randomVec3f(1);
    }
    if (k.key() == 'r') {
      for (int i = 0; i < velocity.size(); i++) {
        mesh.vertices()[i] = randomVec3f(5);
        velocity[i] = randomVec3f(0.1);
        force[i] = randomVec3f(1);
      }
    }
    return true;
  }

  void onDraw(Graphics& g) override {
    if (!isPrimary()) nav().set(state().camera);
 
    g.clear(0.0);
    g.blending(true);
    g.blendTrans();
    g.depthTesting(false); 

    for (int i = 0; i < N; i++) {
        if (!isPrimary()) mesh.vertices()[i] = state().position[i];
        mesh.colors()[i] = state().color[i];
        float normalized_y = (mesh.vertices()[i].y + 5.0f) / 10.0f;
        normalized_y = std::max(0.0f, std::min(normalized_y, 1.0f));
        mesh.colors()[i] = Color(normalized_y * 0.5f, normalized_y * 0.8f + 0.2f, 1.0f, 1.0f);
    }

    g.shader().use(0); 
    g.meshColor();

    // 1. DRAW SMOOTH BEZIER RIBBONS
    if (enableRibbons.get() == 1.0f) {
      Mesh ribbons;
      ribbons.primitive(Mesh::TRIANGLES);

      float h = perceptionRadius.get(); 
      float line_thickness = 0.02f; 
      int segments = 8;

      SpatialHash drawGrid;
      drawGrid.build(mesh.vertices(), h);

      for (int i = 0; i < N; i++) {
        auto neighbors = drawGrid.getNeighbors(mesh.vertices()[i]);
        
        for (int j : neighbors) { 
          if (j <= i) continue; 
          
          Vec3f A = mesh.vertices()[i];
          Vec3f B = mesh.vertices()[j];
          float distance = (B - A).mag();

          if (distance < h && distance > 0.0001f) {
            
            Vec3f dirA = velocity[i];
            if (dirA.mag() < 0.001f) dirA = (B - A);
            dirA.normalize();

            Vec3f dirB = velocity[j];
            if (dirB.mag() < 0.001f) dirB = (B - A);
            dirB.normalize();

            Vec3f P0 = A;
            Vec3f P1 = A + dirA * (distance * 0.35f);
            Vec3f P2 = B - dirB * (distance * 0.35f);
            Vec3f P3 = B;
             
            float dist_t = distance / h;
            float math_opacity = 0.5f * (1.0f - dist_t); 
            float r_color = std::min(2.0f * dist_t, 1.0f);
            float g_color = std::min(2.0f * (1.0f - dist_t), 1.0f);
            Color c(r_color, g_color, 0.0f, math_opacity);

            Vec3f prev_v0, prev_v1;

            for (int k = 0; k <= segments; k++) {
              float t = (float)k / segments;
              float u = 1.0f - t;

              Vec3f p = (u*u*u)*P0 + 3*(u*u)*t*P1 + 3*u*(t*t)*P2 + (t*t*t)*P3;

              Vec3f tangent = 3*u*u*(P1 - P0) + 6*u*t*(P2 - P1) + 3*t*t*(P3 - P2);
              if (tangent.mag() < 0.0001f) tangent = (B - A);
              tangent.normalize();

              Vec3f view_dir = (nav().pos() - p).normalize();
              Vec3f right = cross(tangent, view_dir);
              if (right.mag() < 0.001f) right = Vec3f(1, 0, 0); 
              right.normalize();

              Vec3f offset = right * line_thickness;
              Vec3f v0 = p + offset;
              Vec3f v1 = p - offset;

              if (k > 0) {
                ribbons.vertex(prev_v0); ribbons.color(c);
                ribbons.vertex(prev_v1); ribbons.color(c);
                ribbons.vertex(v0);      ribbons.color(c);
                
                ribbons.vertex(v0);      ribbons.color(c);
                ribbons.vertex(prev_v1); ribbons.color(c);
                ribbons.vertex(v1);      ribbons.color(c);
              }
              prev_v0 = v0;
              prev_v1 = v1;
            }
          }
        }
      }
      
      g.draw(ribbons); 
      g.blendAdd();
    }
    
    // 2. DRAW PARTICLES OR 3D BUBBLES
    if (enableBubbles.get() == 1.0f) {
      g.shader(bubbleShader); 
      g.blending(true);
      g.blendTrans();    
      g.depthTesting(true); 
      g.cullFace(false);

      float b_size = bubbleSize.get();
      
      for (int i = 0; i < N; i++) {
        g.pushMatrix();
        g.translate(mesh.vertices()[i]);
        g.scale(b_size);
        
        Color c = mesh.colors()[i];
        g.color(c.r, c.g, c.b, 0.3f); 
        
        g.draw(sphereMesh); 
        g.popMatrix();
      }

    } else {
      g.blending(true);
      g.blendAdd();
      g.depthTesting(false);
      g.shader(pointShader);
      g.shader().uniform("pointSize", pointSize.get() / 100.0f);
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