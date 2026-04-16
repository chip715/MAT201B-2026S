#include <iostream>

#include "al/app/al_App.hpp"  // al::App
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"
#include "al/math/al_Vec.hpp"

using namespace al;

float r() { return rnd::uniform(); }
float rs() { return rnd::uniformS(); }

// Fixed: Added semicolon after int other
// int pickOther(int me, int n){
// int random_offset = (int)(rnd::uniform() * (n - 1)); 
//   return (me + 1 + random_offset) % n;
// }

struct MyApp : public App {
  ParameterInt N{"/N", "", 10, 2, 100};
  Parameter neighbor_distance{"/n", "", 0.1, 0.01, 1};
  ParameterColor color{"/color"};

  Light light;
  Material material;

  Mesh mesh;

  std::vector<Nav> agent;
//   std::vector<int> lover;
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

  // lover.resize(n);
    knockback.assign(n, Vec3d(0, 0, 0));

    for (int i = 0; i < agent.size(); i++){
  
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


           //reset
        Vec3d average_pos = Vec3d(0, 0, 0);
        Vec3d average_heading = Vec3d(0, 0, 0);
        int neighbor_count = 0;

        //keeping track of who is closest so when we check distance.
        float closest_distance = 10000000.0;
        int closest_index =-1;


        for (int j = 0; j < agent.size(); j++) {
            if (i == j) {   continue;   }
            auto& them = agent[j];

            float distance = (me.pos() - them.pos()).mag();


            if(closest_distance > distance) {
                closest_distance = distance;
                closest_index=j;
            }
            else {
            closest_distance=closest_distance;
            }

            //move away from them if they are close.
        if (distance < 1.0) {
       
        me.nudgeToward(them.pos(), -0.1);

        average_pos += them.pos();
        average_heading += them.uf();
        neighbor_count += 1;
        } 
        else if (distance < neighbor_distance) {
        average_pos += them.pos();
        average_heading += them.uf();
        neighbor_count += 1;
        }
        }

        if(neighbor_count>0){
        average_heading = average_heading / neighbor_count;
        average_pos     = average_pos / neighbor_count;

        me.nudgeToward  (average_pos,0.1);
        me.faceToward   (me.pos() + average_heading, 0.1);
        }

        if (neighbor_count==0) {
        //how do i pick a closest agent and turn toward them
        me.nudgeToward  (agent[closest_index].pos(),0.0001);
        me.faceToward   (agent[closest_index].pos(),0.001);
        }

        //zaxis limit. if it gets too close to the camera.
        // push it back
  
        if((me.pos() - nav().pos()).mag() < 5.0){
        me.nudgeToward  (me.pos() + nav().uf(),0.01);
        me.faceToward   (me.pos()+ nav().uf(),0.01);
        }

        if((me.pos() - nav().pos()).mag() > 50.0){
        me.nudgeToward  (me.pos() + nav().uf(),-0.01);
        me.faceToward   (me.pos()+ nav().uf(),-0.01);
        }

        //wrap around
        //if it is the out side of the camaera view
        float distance = nav().pos().z - me.pos().z;

        //calculate relative agent position to camera
        Vec3f relPos =  me.pos()-nav().pos();
        float relX = relPos.dot(nav().ux());
        float relY = relPos.dot(nav().uy());
        float relZ = relPos.dot(nav().uf()); 

  
        float edge = relZ * tan((lens().fovy() / 2.0) * (M_PI / 180.0));
        float x_edge = edge * (float(width()) / height());

        if (relX > x_edge) {
        me.pos() -= nav().ux() * (x_edge * 2 - 0.2);
        }
        if (relX < -x_edge) {
        me.pos() += nav().ux() * (x_edge * 2 - 0.2);
        }
        if (relY > x_edge) {
        me.pos() -= nav().uy() * (edge * 2 - 0.2);
        }
        if (relY < -x_edge) {
        me.pos() += nav().uy() * (edge * 2 - 0.2);
        }
     
        
    }

    // Re-assign "lovers" 
    // if(t > 1) {
    //   t -= 1;
    //   for (int i = 0; i < agent.size(); i++){
    //     lover[i] = pickOther(i, agent.size());
    //   }
    // }
    // t += dt;

    // Chase our love 
   
    // for (int i = 0; i < agent.size(); i++) {
    //   auto& me = agent[i];
    //   int love = lover[i];
    //   auto& them = agent[love];
    //    me.nudgeToward(them.pos(), 0.009);
    //    me.faceToward(them.pos(), 0.01);
       
        
    //  }
    

    // Movement & Physics
    for (int i = 0; i<agent.size(); i++) {
      auto& a = agent[i]; 
      a.moveF(0.7);
    //   a.pos() += knockback[i] * dt;      
    //   knockback[i] *= 0.9;
    }

    for (auto& a : agent) {
      a.step(dt);
    }
  } 
  

  void onDraw(Graphics& g) override {


    g.clear(color);

    light.ambient(RGB(1));          // Ambient reflection for this light
    light.diffuse(RGB(0.5, 0.7, 0.5));  // Light scattered directly from light
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