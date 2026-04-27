// Karl Yerkes
// 2022-01-20

#include "al/app/al_App.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"
#include "al/ui/al_Parameter.hpp"

using namespace al;

#include <fstream>
#include <vector>
using namespace std;

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}
string slurp(string fileName);  // forward declaration


struct AlloApp : App {
  Parameter pointSize{"/pointSize", "", 2.0, 1.0, 10.0};
  Parameter timeStep{"/timeStep", "", 0.1, 0.01, 0.6};
  Parameter dragFactor{"/dragFactor", "", 0.1, 0.0, 0.9};
  Parameter springStiffness{"String Stiffness", 0.1, 0.0, 0.9};
  Parameter springLength{"Spring Length", 1, 0, 50};
  Parameter repulsivity{"Repulsivity", 1, 0, 50};
  //

  ShaderProgram pointShader;

  //  simulation state
  Mesh mesh;  // position *is inside the mesh* mesh.vertices() are the positions
  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;

  void onInit() override {
    // set up GUI
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(pointSize);  // add parameter to GUI
    gui.add(timeStep);   // add parameter to GUI
    gui.add(dragFactor);   // add parameter to GUI
    gui.add(springStiffness);
    gui.add(springLength);
    gui.add(repulsivity);
    //
  }

  void onCreate() override {
    // compile shaders
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));

    // set initial conditions of the simulation
    //

    // c++11 "lambda" function
    auto randomColor = []() { return HSV(rnd::uniform(), 1.0f, 1.0f); };

    mesh.primitive(Mesh::POINTS);
    // does 1000 work on your system? how many can you make before you get a low
    // frame rate? do you need to use <1000?
    for (int _ = 0; _ < 500; _++) {
      mesh.vertex(randomVec3f(5));
      mesh.color(randomColor());

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
    if (freeze) return;

    // 

    // XXX you put code here that calculates spring forces and sets
    // accelerations These are pair-wise. Each unique pairing of two particles
    // These are equal but opposite: A exerts a force on B while B exerts that
    // same amount of force on A (but in the opposite direction!) Use a nested
    // for loop to visit each pair once The time complexity is O(n*n)
    //
    // Vec3f has lots of operations you might use...
    // • +=
    // • -=
    // • +
    // • -
    // • .normalize() ~ Vec3f points in the direction as it did, but has length 1
    // • .normalize(float scale) ~ same but length `scale`
    // • .mag() ~ length of the Vec3f
    // • .magSqr() ~ squared length of the Vec3f
    // • .dot(Vec3f f) 
    // • .cross(Vec3f f)
    for (int i = 0; i < velocity.size(); i++) {
      // calculate spring force between this particle and the origin

      auto& me = mesh.vertices()[i];
      float actual_length = me.mag(); // how far am i from the origin?
      float force_amount = (springLength - actual_length) * springStiffness;//hookslaw
     
      Vec3f force_direction = me;
      force_direction.normalize();

      force[i] += force_direction * force_amount;
      
    }


    // Calculate repulsive forces....

     for (int i = 0; i < mesh.vertices().size(); ++i) {
      for (int j = i + 1; j < mesh.vertices().size(); ++j) {
        // what is the *direction* of the force? (with magnitude of 1)

        Vec3f force_direction = (mesh.vertices()[i] - mesh.vertices()[j]);
        float distance = force_direction.mag(); // aka "r"
        force_direction /= distance; // the same as .normalize() in this case

        float force_amount = repulsivity * mass[i] * mass[j] / (distance * distance);

        // i and j are a pair
        // limit large forces... if the force is too large, ignore it

        if (force_amount > 10) {
          force_amount = 10;
        }

        force[i] += force_direction * force_amount;
        force[j] -= force_direction * force_amount;
      }
    }

///////// you probably do not need to edit the rest of this method...

    //viscous drag
    //
    for (int i = 0; i < velocity.size(); i++) {
      force[i] += - velocity[i] * dragFactor; // F = -bv
    }

    // Numerical Integration
    //
    vector<Vec3f> &position(mesh.vertices());
    for (int i = 0; i < velocity.size(); i++) {
      // "semi-implicit" Euler integration
      velocity[i] += force[i] / mass[i] * timeStep;
      position[i] += velocity[i] * timeStep;
    }

    // clear all accelerations (IMPORTANT!!)
    //
    for (auto &a : force) a.set(0);
  }

  bool onKeyDown(const Keyboard &k) override {
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

    return true;
  }

  void onDraw(Graphics &g) override {
    g.clear(0.3);
    g.shader(pointShader);
    g.shader().uniform("pointSize", pointSize / 100);
    g.blending(true);
    g.blendTrans();
    g.depthTesting(true);
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
