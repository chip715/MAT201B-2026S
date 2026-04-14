#include <iostream>

#include "al/app/al_App.hpp"  // al::App
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float r() { return rnd::uniform(); }
float rs() { return rnd::uniformS(); }

struct MyApp : public App {
  ParameterInt N{"/N", "", 10, 2, 100};
  Parameter neighbor_distance{"/n", "", 0.1, 0.01, 1};
  ParameterColor color{"/color"};

  Light light;
  Material material;

  Mesh mesh;

  std::vector<Nav> agent;

  void onInit() override {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(N);
    gui.add(color);
    gui.add(neighbor_distance);
    //
  }
  
  void reset(int n) {
    agent.clear();
    agent.resize(n);
    for (auto& a : agent) {
      a.pos(Vec3d(rs(), rs(), rs()));
      a.quat(Quatd(Vec3d(rs(), rs(), rs())).normalize());
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
          // ....
          // if me is too close, move me away
          // find center?
          sum += them.pos();
        }
        sum += me.pos();
        if (count > 1) {
          Vec3d center = sum / (count + 1);
          // if me is to far from center, move me toward center
        }
      }
    }


    for (auto& a : agent) {
      a.turnR(0.035);
      a.moveF(0.7);
    }

    //
    //
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

    //std::cout << g.modelMatrix().mElems[0] << std::endl;

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
