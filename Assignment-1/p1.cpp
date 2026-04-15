#include <iostream>

#include "al/app/al_App.hpp"  // al::App
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float r() { return rnd::uniform(); }
float rs() { return rnd::uniformS(); }

// Fixed: Added semicolon after int other
int pickOther(int me, int n){
  int other; 
  do {
    other = rnd::uniform(0, n-1);
  } while (me == other);
  return other;
}

struct MyApp : public App {
  ParameterInt N{"/N", "", 10, 2, 100};
  Parameter neighbor_distance{"/n", "", 0.1, 0.01, 1};
  ParameterColor color{"/color"};

  Light light;
  Material material;

  Mesh mesh;

  std::vector<Nav> agent;
  std::vector<int> lover;
  std::vector<Vec3d> knockback;
  
  double t = 0; // Fixed: Added 't' to keep track of time for the 5-second timer

  void onInit() override {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    

    
    auto &gui = GUIdomain->newGUI();
    gui.add(N);
    gui.add(color);
    gui.add(neighbor_distance);

   
  }
  
  void reset(int n) {
    agent.clear();
    agent.resize(n);
    for (auto& a : agent) {
      a.pos(Vec3d(rs(), rs(), rs()));
      a.quat(Quatd(Vec3d(rs(), rs(), rs())).normalize());
    }

    lover.resize(n); // Fixed: Added 'n' so the vector knows what size to be
    knockback.assign(n, Vec3d(0, 0, 0));

    for (int i = 0; i < agent.size(); i++){
      lover[i] = pickOther(i, agent.size()); // Using your handy function!
    }
  }

  void onCreate() override {
   
    addCone(mesh);
    mesh.scale(1, 0.2, 1);
    mesh.scale(0.2);
    mesh.generateNormals();

    nav().pos(0, 0, 6);
    light.pos(-2, 7, 0);
  }

  int lastN = 0;
void onAnimate(double dt) override {

    if (N != lastN) {
      lastN = N;
      reset(N);
    }

    
    for (int i = 0; i < agent.size(); i++) {
      auto& me = agent[i];
      for (int j = 0; j < agent.size(); j++) {
        if (i == j) {
          continue;
        }

        auto& them = agent[j];

        // me versus them
        Vec3d sum;
        int count = 0;
        float distance = (me.pos() - them.pos()).mag();
        if (distance < neighbor_distance) {
          count++;
          // them is a neighbor
          // if me is too close, move me away
          sum += them.pos();
        }
        sum += me.pos();
        if (count > 1) {
          Vec3d center = sum / (count + 1);
          // if me is to far from center, move me toward center
        }
      }
    }

    // Re-assign "lovers" every 7 seconds
    if(t > 7) {
      t -= 7;
      for (int i = 0; i < agent.size(); i++){
        lover[i] = pickOther(i, agent.size());
      }
    }
    t += dt;

    // Chase our love 
    for (int i = 0; i < agent.size(); i++) {
      auto& me = agent[i];
      int love = lover[i];
      auto& them = agent[love];

      float distance = (me.pos() - them.pos()).mag();
      
      if(distance < 0.5) {
        // If we get too close, nudge away
        Vec3d push_dir = (me.pos() - them.pos()).normalize();
        knockback[i] = push_dir * 25.0;
        lover[i] = pickOther(i, agent.size());

      } else {
        // Otherwise, keep turning toward them
        me.faceToward(them.pos(), 0.015);
      }
    }

    // Movement & Physics
    for (int i = 0; i<agent.size(); i++) {
      auto& a = agent[i]; // Fixed the colon to an equals sign here!
      a.moveF(0.8);
      a.pos() += knockback[i] * dt;      
      knockback[i] *= 0.9;
    }

    for (auto& a : agent) {
      a.step(dt);
    }
  } 
  

  void onDraw(Graphics& g) override {


    g.clear(color);

    light.ambient(RGB(0));          // Ambient reflection for this light
    light.diffuse(RGB(1, 1, 0.5));  // Light scattered directly from light
    g.lighting(true);
    g.light(light);
    material.specular(light.diffuse() * 0.2);  // Specular highlight, "shine"
    material.shininess(50);  // Concentration of specular component [0,128]

    g.material(material);

    for (auto& a : agent) {
      g.pushMatrix();
      g.translate(a.pos());
      g.rotate(a.quat());
      g.draw(mesh);
      g.popMatrix();
    }
  }
};

int main() { MyApp().start(); }