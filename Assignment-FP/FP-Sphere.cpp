// Karl Yerkes
// 2022-01-20

#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/math/al_Random.hpp"
#include "al_ext/statedistribution/al_CuttleboneDomain.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/ui/al_Parameter.hpp" 

#include <algorithm>
#include <fstream>
#include <random>
#include <vector>
#include <unordered_map>
#include <iostream>

using namespace al;
using namespace std;

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}

string slurp(string fileName);  

const int MAX_N = 500; 

struct SpatialHash {
  float cellSize;
  std::unordered_map<unsigned long long, std::vector<int>> cells;

  void build(const std::vector<Vec3f>& positions, float radius, int count) {
    cellSize = radius;
    cells.clear();
    for (int i = 0; i < count; ++i) {
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
  Vec3f position[MAX_N]; 
  Color color[MAX_N];

  int syncActiveParticles;
  int syncMaxRibbons;
  float syncPointSize;
  float syncFocalDepth;
  float syncBubbleSize;

  bool syncEnableSpring;
  bool syncSpringToOrigin;
  bool syncSpringToCamera;
  bool syncPinSpringToFront;
  bool syncEnableSwarm;
  bool syncEnableWarp;
  bool syncEnableRibbons;
  bool syncCurvyLines;
  bool syncEnableBubbles;
  bool syncEnableOrbit;
  bool syncOrbitAroundOrigin;
  bool syncEnableClamp;
};

struct Keyframe {
    string name;
    float duration;
    Vec3f pos;
    Quatf rot;
    
    int activeParticles;
    int maxRibbons;
    float pointSize;
    float timeStep;
    float dragFactor;
    float springStiffness;
    float springLength;
    float focalDepth;
    float perceptionRadius;
    float repulsionStrength;
    float desiredPersonalSpace;
    float viscosity;
    float bubbleSize;
    float orbitSpeed;
    float orbitAxisX;
    float orbitAxisY;
    float orbitAxisZ;

    float enableSpring;
    float springToOrigin;
    float springToCamera;
    float pinSpringToFront;
    float enableSwarm;
    float enableWarp;
    float enableRibbons;
    float curvyLines;
    float enableBubbles;
    float enableOrbit;
    float orbitAroundOrigin;
    float enableClamp;
};

class Timeline {
public:
    vector<Keyframe> events;
    int currentIndex = 0;
    float timer = 0.0f;
    bool active = false;

    void load(const string& filename) {
        events.clear();
        ifstream file(filename);
        if (!file.is_open()) {
            return;
        }
        string line;
        while (getline(file, line)) {
            if (line.find("{") != string::npos) {
                Keyframe k;
                events.push_back(k);
            }
        }
    }
};

struct AlloApp : DistributedAppWithState<WorldState> {
  //=================================================================
  // CORE LAYOUT VARIABLE DECLARATIONS
  //=================================================================
  ParameterInt activeParticles{"Active Particles", "", 279, 10, MAX_N};
  Parameter pointSize       {"Point Size", "", 4.083f, 1.0f, 10.0f};
  Parameter timeStep        {"Time Step", "", 0.136f, 0.01f, 0.6f};
  Parameter dragFactor      {"Drag Factor", "", 0.700f, 0.0f, 0.9f};
  ParameterBool enableWarp  {"Enable Warp", "", 0.0f};

  // Boundary Control
  ParameterBool enableClamp   {"Enable Distance Clamp", "Boundary Control", 0.0f};

  // Controls Menu
  ParameterBool saveKeyframe    {"Save Keyframe", "Controls", 0.0f};
  ParameterBool playAutomation  {"Play Automation", "Controls", 0.0f};
  ParameterBool pauseAutomation {"Pause Automation", "Controls", 0.0f};
  ParameterBool stopAutomation  {"Stop Automation", "Controls", 0.0f};
  Parameter transitionSpeed     {"Transition Speed Factor", "Controls", 1.000f, 0.1f, 10.0f};
  Parameter holdDuration        {"Keyframe Hold Time (sec)", "Controls", 2.000f, 0.0f, 10.0f};

  // Orbit Settings Submenu
  ParameterBool enableOrbit   {"Enable Orbit", "Orbit Settings", 1.0f};
  ParameterBool orbitAroundOrigin{"Orbit Around Origin", "Orbit Settings", 1.0f};
  Parameter orbitSpeed        {"Orbit Speed", "Orbit Settings", 0.034f, -15.0f, 15.0f};
  Parameter orbitAxisX        {"Orbit Axis X", "Orbit Settings", -0.880f, -1.0f, 1.0f};
  Parameter orbitAxisY        {"Orbit Axis Y", "Orbit Settings", -0.402f, -1.0f, 1.0f};
  Parameter orbitAxisZ        {"Orbit Axis Z", "Orbit Settings", 1.000f, -1.0f, 1.0f};
  ParameterString orbitStatusText{"Orbit Center Status", "Orbit Settings", "Orbit center: Origin"};

  // Ribbon Settings Submenu
  ParameterBool enableBubbles {"Enable Bubbles", "Ribbon Settings", 1.0f};  
  Parameter bubbleSize       {"Bubble Size", "Ribbon Settings", 1.673f, 0.01f, 5.0f};
  ParameterBool enableRibbons{"Enable Ribbons", "Ribbon Settings", 1.0f};
  ParameterBool curvyLines   {"Curvy Lines", "Ribbon Settings", 1.0f}; 
  ParameterInt maxRibbons    {"Max Ribbons", "Ribbon Settings", 10, 1, 25};

  // Spring Settings Submenu
  ParameterBool enableSpring   {"Enable Spring", "Spring Settings", 1.0f};
  ParameterBool springToOrigin {"Spring Center to Origin", "Spring Settings", 1.0f};
  ParameterBool springToCamera {"Spring Center to Camera", "Spring Settings", 0.0f};
  ParameterBool pinSpringToFront{"Pin Spring to Camera Front", "Spring Settings", 0.0f};
  Parameter springStiffness    {"Spring Stiffness", "Spring Settings", 0.718f, 0.0f, 0.9f};
  Parameter springLength       {"Spring Length", "Spring Settings", 16.437f, 0.0f, 50.0f};
  Parameter focalDepth         {"Focal Depth", "Spring Settings", 45.632f, -10.0f, 100.0f};
  ParameterString springStatusText{"Spring Center Status", "Spring Settings", "Spring center: origin"};

  // Telemetry Status Monitoring Displays
  ParameterInt currentFrameNum{"Current Frame Index", "Status", 0, 0, 100};
  ParameterString timelineStatus{"Timeline Status", "Status", "Stopped"};
  ParameterVec3 cameraPosDisplay{"Camera Position (XYZ)", "Status", Vec3f(0.0f)};
  ParameterString sequencingPhase{"Current Sequence Phase", "Status", "Idle"};
  
  // Swarm Settings Submenu
  ParameterBool enableSwarm     {"Enable Swarm", "Swarm Settings", 1.0f};
  Parameter perceptionRadius    {"Perception Radius", "Swarm Settings", 15.786f, 0.5f, 20.0f}; 
  Parameter repulsionStrength   {"Repulsion Strength", "Swarm Settings", 48.793f, 1.0f, 100.0f};    
  Parameter desiredPersonalSpace{"Personal Space", "Swarm Settings", 5.500f, 1.0f, 20.0f};     
  Parameter viscosity           {"Viscosity", "Swarm Settings", 1.400f, 0.0f, 10.0f};

  ShaderProgram bubbleShader; 
  ShaderProgram pointShader;
  ShaderProgram ribbonShader;

  Mesh mesh;
  Mesh sphereMesh; 
  Timeline timeline;

  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;

  void onInit() override {
  // 1. Allosphere safe Cuttlebone initialization
  //for running on sphere
    auto cuttleboneDomain = CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    if (!cuttleboneDomain) {
      std::cerr << "WARNING: Cuttlebone failed to start. Running local mode fallback." << std::endl;
    }


    if (isPrimary()) {
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();

        // 1. General Configuration Sliders
        gui.add(activeParticles);
        gui.add(pointSize); 
        gui.add(timeStep);   
        gui.add(dragFactor);  
        gui.add(enableWarp);

        // 2. Damping Bounds
        gui.add(enableClamp);

        // 3. Automation Playback System Controls Folder
        gui.add(saveKeyframe);
        gui.add(playAutomation);
        gui.add(pauseAutomation);
        gui.add(stopAutomation);
        gui.add(transitionSpeed);  
        gui.add(holdDuration);     

        // 4. Orbit Settings Collapsible Folder
        gui.add(enableOrbit);
        gui.add(orbitAroundOrigin);
        gui.add(orbitSpeed);
        gui.add(orbitAxisX);
        gui.add(orbitAxisY);
        gui.add(orbitAxisZ);
        gui.add(orbitStatusText);

        // 5. Ribbon Settings Collapsible Folder
        gui.add(enableBubbles);  
        gui.add(bubbleSize);
        gui.add(enableRibbons);
        gui.add(curvyLines); 
        gui.add(maxRibbons);

        // 6. Spring Focus Settings Collapsible Folder
        gui.add(enableSpring);
        gui.add(springToOrigin);
        gui.add(springToCamera);
        gui.add(pinSpringToFront);
        gui.add(springStiffness);
        gui.add(springLength);
        gui.add(focalDepth);
        gui.add(springStatusText);

        // 7. Dynamic Telemetry Status Layout Monitor Group
        gui.add(currentFrameNum);
        gui.add(timelineStatus);
        gui.add(cameraPosDisplay);
        gui.add(sequencingPhase);

        // 8. Swarm Interaction Settings Folder (Locked to bottom)
        gui.add(enableSwarm);
        gui.add(perceptionRadius);
        gui.add(repulsionStrength);
        gui.add(desiredPersonalSpace);
        gui.add(viscosity);
        
        timeline.load("../events.txt");
    }

    parameterServer() << pointSize << timeStep << dragFactor << springStiffness 
                      << springLength << focalDepth << perceptionRadius 
                      << repulsionStrength << desiredPersonalSpace << viscosity 
                      << bubbleSize << enableSpring << springToOrigin << springToCamera << pinSpringToFront
                      << enableSwarm << enableWarp << enableRibbons 
                      << enableBubbles << saveKeyframe
                      << playAutomation << pauseAutomation << stopAutomation
                      << transitionSpeed << holdDuration
                      << enableOrbit << orbitAroundOrigin << orbitSpeed 
                      << orbitAxisX << orbitAxisY << orbitAxisZ << enableClamp;
  }

  void onCreate() override {
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));
                        
    bubbleShader.compile(slurp("../bubble-vertex.glsl"),
                         slurp("../bubble-fragment.glsl"));

    ribbonShader.compile(slurp("../ribbon-vertex.glsl"),
                         slurp("../ribbon-fragment.glsl"));

    addSphere(sphereMesh, 1.0, 32, 32);
    sphereMesh.generateNormals(); 
                        
    mesh.primitive(Mesh::POINTS);

    for (int _ = 0; _ < MAX_N; _++) {
      mesh.vertex(randomVec3f(5));
      mesh.color(1.0, 1.0, 1.0);  

      float m = 3 + rnd::normal() / 2;
      if (m < 0.5) m = 0.5;
      mass.push_back(m);

      mesh.texCoord(pow(m, 1.0f / 3), 0);  

      velocity.push_back(randomVec3f(0.1));
      force.push_back(randomVec3f(1));
    }
    
    for(int i = 0; i < MAX_N; i++){
      mesh.vertices()[i].normalize();
    }

    nav().pos(-92.454f, -0.847f, 29.446f);
  }
  
  bool freeze = false;
  bool isHolding = false; 

  void onAnimate(double dt) override {
    if (!isPrimary()) return;
    
    state().frame++;
    state().time += dt;

    if (freeze) return;

    // Mutually exclusive anchor evaluations
    static bool lastSpringOrigin = (springToOrigin.get() == 1.0f);
    static bool lastSpringCamera = (springToCamera.get() == 1.0f);
    static bool lastPinFront = (pinSpringToFront.get() == 1.0f);

    bool currentOrigin = (springToOrigin.get() == 1.0f);
    bool currentCamera = (springToCamera.get() == 1.0f);
    bool currentPinFront = (pinSpringToFront.get() == 1.0f);

    if (currentOrigin && !lastSpringOrigin) {
        springToCamera.set(0.0f);
        pinSpringToFront.set(0.0f);
    } else if (currentCamera && !lastSpringCamera) {
        springToOrigin.set(0.0f);
        pinSpringToFront.set(0.0f);
    } else if (currentPinFront && !lastPinFront) {
        springToOrigin.set(0.0f);
        springToCamera.set(0.0f);
    }

    lastSpringOrigin = (springToOrigin.get() == 1.0f);
    lastSpringCamera = (springToCamera.get() == 1.0f);
    lastPinFront = (pinSpringToFront.get() == 1.0f);

    if (springToOrigin.get() == 1.0f) {
        springStatusText.set("Spring center: origin");
    } else if (springToCamera.get() == 1.0f) {
        springStatusText.set("Spring center: camera");
    } else if (pinSpringToFront.get() == 1.0f) {
        springStatusText.set("Spring center: distance away from camera");
    }

    if (orbitAroundOrigin.get() == 1.0f) {
        orbitStatusText.set("Orbit center: Origin");
    } else {
        orbitStatusText.set("Orbit center: camera");
    }

    if (playAutomation.get()) {
        timeline.active = true;
        timelineStatus.set("Playing");
        playAutomation.set(0); 
    }
    if (pauseAutomation.get()) {
        timeline.active = false;
        timelineStatus.set("Paused");
        sequencingPhase.set("Paused");
        pauseAutomation.set(0); 
    }
    if (stopAutomation.get()) {
        timeline.active = false;
        timeline.currentIndex = 0;
        timeline.timer = 0.0f;
        isHolding = false;
        timelineStatus.set("Stopped");
        sequencingPhase.set("Stopped");
        currentFrameNum.set(0);
        stopAutomation.set(0); 
    }

    // Capture camera vectors AFTER automation updates have finalized positions
    Vec3f camPos(nav().pos());
    Vec3f ur(nav().ur()); 
    Vec3f uu(nav().uu()); 
    Vec3f uf(nav().uf());
    cameraPosDisplay.set(camPos);

    int current_N = activeParticles.get();

    // SPRING AND SWARM SIMULATION PHYSICS FORCES
    if (enableSpring.get() == 1.0f) {
      Vec3f focal_point;
      if (springToOrigin.get() == 1.0f) {
          focal_point = Vec3f(0.0f, 0.0f, 0.0f);
      } else if (springToCamera.get() == 1.0f) {
          focal_point = camPos;
      } else if (pinSpringToFront.get() == 1.0f) {
          focal_point = camPos + (uf * focalDepth.get()); 
      } else {
          focal_point = Vec3f(0.0f, 0.0f, 0.0f); 
      }

      Vec3f customOrbitAxis(orbitAxisX.get(), orbitAxisY.get(), orbitAxisZ.get());
      if (customOrbitAxis.magSqr() < 0.0001f) {
          customOrbitAxis.set(0.0f, 1.0f, 0.0f);
      }
      customOrbitAxis.normalize();

      for (int i = 0; i < current_N; i++) {
        auto& me = mesh.vertices()[i];
        Vec3f dir = focal_point - me;
        float dist = dir.mag(); 
        
        if (dist > 0.001f) {
          Vec3f pullDir = dir / dist; 
          float force_amount = (dist - springLength.get()) * springStiffness.get();
          force[i] += pullDir * force_amount;
        }

        if (enableOrbit.get() == 1.0f) {
            Vec3f orbitCenter = (orbitAroundOrigin.get() == 1.0f) ? Vec3f(0.0f, 0.0f, 0.0f) : camPos;
            Vec3f relativeToCenter = me - orbitCenter;
            float centerDist = relativeToCenter.mag();
            
            if (centerDist > 0.001f) {
              Vec3f radialDir = relativeToCenter / centerDist;
              
              Vec3f tangentDir = cross(radialDir, customOrbitAxis);
              if (tangentDir.magSqr() < 0.0001f) {
                  tangentDir = (abs(customOrbitAxis.z) < 0.9f) ? Vec3f(0,0,1) : Vec3f(1,0,0);
              }
              tangentDir.normalize();

              force[i] += tangentDir * orbitSpeed.get() * mass[i];
            }
        }
      }
    }

    if(enableSwarm.get() == 1.0f){
      float radius = perceptionRadius.get();              
      float target_space = desiredPersonalSpace.get();    
      float repulsion_factor = repulsionStrength.get();   
      float friction_factor = viscosity.get();            
      float radius2 = radius * radius;
      
      float proximity_weight    = 315.0f / (64.0f * 3.14159265f * pow(radius, 9));
      float repulsion_gradient  = -45.0f / (3.14159265f * pow(radius, 6));
      float alignment_weight    = 45.0f / (3.14159265f * pow(radius, 6));

      std::vector<float> crowdedness(MAX_N, 0.0f);
      std::vector<float> claustrophobia(MAX_N, 0.0f);

      SpatialHash physicsGrid;
      physicsGrid.build(mesh.vertices(), radius, current_N);

      for (int i = 0; i < current_N; ++i) {
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

      float b_size = bubbleSize.get();
      float min_dist = b_size * 2.0f; 
      float barrier_threshold = 0.5f;

      for (int i = 0; i < current_N; ++i) {
        auto neighbors = physicsGrid.getNeighbors(mesh.vertices()[i]);
        for (int j : neighbors) {
          if (j <= i) continue; 
          
          Vec3f dir = mesh.vertices()[i] - mesh.vertices()[j];
          float r = dir.mag();

          if (r < radius && r > 0.001f) {
            dir /= r; 

            float push_mag = 0.0f;
            bool used_barrier = false;

            float avg_mass = (mass[i] + mass[j]) / 2.0f;
            float avg_crowding = (crowdedness[i] + crowdedness[j]) / 2.0f;

            if (enableBubbles.get() == 1.0f) {
                float gap = r - min_dist;
                if (gap < barrier_threshold) { 
                    float penetration = barrier_threshold - gap;
                    push_mag = penetration * repulsionStrength.get() * 5.0f; 
                    used_barrier = true;
                }
            }

            if (!used_barrier) {
                float shared_claustrophobia = (claustrophobia[i] + claustrophobia[j]) / 2.0f;
                float kernel_grad = repulsion_gradient * (radius - r) * (radius - r);
                push_mag = -shared_claustrophobia * kernel_grad; 
            }

            Vec3f v_diff = velocity[j] - velocity[i];
            float kernel_lap = alignment_weight * (radius - r);

            float total_neighbors = (float)(neighbors.size()); 
            float safe_count = (total_neighbors > 1.0f) ? total_neighbors : 1.0f;

            Vec3f alignment_force = v_diff * (friction_factor * avg_mass * kernel_lap / safe_count);
            Vec3f total_force = (dir * push_mag) + alignment_force;
            force[i] += total_force; 
            force[j] -= total_force; 
          }
        }
      }
    }
    
    for (int i = 0; i < current_N; i++) force[i] += - velocity[i] * dragFactor.get(); 

    // --- 4. NUMERICAL INTEGRATION MODULE ---
    vector<Vec3f> &position(mesh.vertices());
    for (int i = 0; i < current_N; i++) {
      velocity[i] += force[i] / mass[i] * timeStep.get();
      position[i] += velocity[i] * timeStep.get();

      float normalized_y = (position[i].y + 5.0f) / 10.0f;
      normalized_y = std::max(0.0f, std::min(normalized_y, 1.0f));
      state().color[i] = Color(normalized_y * 0.5f, normalized_y * 0.8f + 0.2f, 1.0f, 1.0f);

      // DISTANCE CONTROL CLAMP
      Vec3f systemCenter = (springToOrigin.get() == 1.0f) ? Vec3f(0.0f, 0.0f, 0.0f) : camPos;
      Vec3f systemRelPos = position[i] - systemCenter;
      float distToCenter = systemRelPos.mag();
      
      float maxSystemRadius = 45.0f; 
      float absoluteMaxThreshold = 250.0f; 

      if (distToCenter > maxSystemRadius && distToCenter > 0.001f) {
          Vec3f outboundDir = systemRelPos / distToCenter;
          
          if (enableClamp.get() == 1.0f) {
              float normalVelocity = velocity[i].dot(outboundDir);
              if (normalVelocity > 0.0f) {
                  float overshootFactor = (distToCenter - maxSystemRadius) / 10.0f;
                  float dampingMultiplier = 1.0f + overshootFactor * 5.0f;
                  velocity[i] -= outboundDir * normalVelocity * std::min(dampingMultiplier * timeStep.get(), 1.0f);
              }
              
              if (distToCenter > absoluteMaxThreshold) {
                  position[i] = systemCenter + outboundDir * absoluteMaxThreshold;
                  velocity[i] -= outboundDir * velocity[i].dot(outboundDir);
              }
          }
      }

      // =================================================================
      // GENTLE BACKGROUND DEPTH RECYCLING FRUSTUM ENGINE
      // =================================================================
      if (enableWarp.get() == 1.0f) { 
          Vec3f relPos = position[i] - camPos; 
          float relX = relPos.dot(ur);  
          float relY = relPos.dot(uu);  
          float relZ = relPos.dot(uf);  

          if (relZ > 0.01f) { 
              float edge = relZ * tan((lens().fovy() / 2.0f) * (3.14159265f / 180.0f)); 
              float x_edge = edge * (float(width()) / height()); 

              // Horizontal / Vertical side wrapping
              if (relX > x_edge)  position[i] -= ur * (x_edge * 2.0f); 
              if (relX < -x_edge) position[i] += ur * (x_edge * 2.0f); 
              if (relY > edge)   position[i] -= uu * (edge * 2.0f); 
              if (relY < -edge)  position[i] += uu * (edge * 2.0f); 
          }

          // DYNAMIC SLIDER INTEGRATED DEPTH MATRIX WRAPPING
          // Instead of hardcoding 5 to 65, we derive boundaries from your Focal Depth configuration.
          float activeHorizon = focalDepth.get();
          if (activeHorizon < 20.0f) activeHorizon = 45.632f; // Safety floor fallback

          float maxDepthBound = activeHorizon + 35.0f; 
          float softPushOffset = 25.0f; // The gentle background shift step increment

          // GENTLE DEEP-PLANE HORIZON RESET
          // When a particle drifts past the deep zone, gently push it forward by a 
          // small step, keeping it locked inside the distant background view pool.
          if (relZ > maxDepthBound) {
              position[i] -= uf * softPushOffset; 
              
              // Invert velocity component along view vector to break oscillation loops
              float zVel = velocity[i].dot(uf);
              if (zVel > 0.0f) velocity[i] -= uf * (zVel * 2.0f);
          }

          // GENTLE CLOSE-PLANE SAFETY PASS
          // If a swarm node drifts too close to your lens (under 12 units out), 
          // push it back deeper into the scene landscape space smoothly.
          float microCloseFloor = 12.0f;
          if (relZ < microCloseFloor) {
              position[i] += uf * softPushOffset;
              
              float zVel = velocity[i].dot(uf);
              if (zVel < 0.0f) velocity[i] -= uf * (zVel * 2.0f);
          }
      }
      // =================================================================
    }

    if (enableBubbles.get() == 1.0f) {
        float min_dist = bubbleSize.get() * 2.0f;
        for (int iter = 0; iter < 3; iter++) {
            for (int i = 0; i < current_N; i++) {
                for (int j = i + 1; j < current_N; j++) {
                    Vec3f dir = position[i] - position[j];
                    float dist = dir.mag();
                    if (dist < min_dist && dist > 0.001f) {
                        float overlap = min_dist - dist;
                        dir.normalize();
                        float total_mass = mass[i] + mass[j];
                        position[i] += dir * (overlap * (mass[j] / total_mass));
                        position[j] -= dir * (overlap * (mass[i] / total_mass));
                    }
                }
            }
        }
    }

    state().syncActiveParticles = activeParticles.get();
    state().syncMaxRibbons      = maxRibbons.get();
    state().syncPointSize       = pointSize.get();
    state().syncFocalDepth      = focalDepth.get();
    state().syncBubbleSize      = bubbleSize.get();
    
    state().syncEnableSpring   = (enableSpring.get() == 1.0f);
    state().syncSpringToOrigin = (springToOrigin.get() == 1.0f);
    state().syncSpringToCamera = (springToCamera.get() == 1.0f);
    state().syncPinSpringToFront = (pinSpringToFront.get() == 1.0f);
    state().syncEnableSwarm    = (enableSwarm.get() == 1.0f);
    state().syncEnableWarp     = (enableWarp.get() == 1.0f);
    state().syncEnableRibbons  = (enableRibbons.get() == 1.0f);
    state().syncCurvyLines     = (curvyLines.get() == 1.0f);
    state().syncEnableBubbles  = (enableBubbles.get() == 1.0f);
    state().syncEnableOrbit    = (enableOrbit.get() == 1.0f);
    state().syncOrbitAroundOrigin = (orbitAroundOrigin.get() == 1.0f);
    state().syncEnableClamp    = (enableClamp.get() == 1.0f);

    for (auto &a : force) a.set(0); 
    for (int i = 0; i < current_N; i++) state().position[i] = mesh.vertices()[i];
    state().camera.set(nav());  
  }

  bool onKeyDown(const Keyboard& k) override {
    if (k.key() == ' ') freeze = !freeze;
    if (k.key() == '1') {
      for (int i = 0; i < MAX_N; i++) force[i] += randomVec3f(1);
    }
    if (k.key() == 'r') {
      for (int i = 0; i < MAX_N; i++) {
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

    int current_N = state().syncActiveParticles;
    int connection_limit = state().syncMaxRibbons;

    for (int i = 0; i < current_N; i++) {
        if (!isPrimary()) mesh.vertices()[i] = state().position[i];
        mesh.colors()[i] = state().color[i];
        float normalized_y = (mesh.vertices()[i].y + 5.0f) / 10.0f;
        normalized_y = std::max(0.0f, std::min(normalized_y, 1.0f));
        mesh.colors()[i] = Color(normalized_y * 0.5f, normalized_y * 0.8f + 0.2f, 1.0f, 1.0f);
    }

    g.shader().use(0); 
    g.meshColor(); 

    if (state().syncEnableRibbons) {
      Mesh ribbons;
      ribbons.primitive(Mesh::TRIANGLES);

      float h = perceptionRadius.get(); 
      float line_thickness = 0.05f; 
      int segments = state().syncCurvyLines ? 8 : 1; 

      SpatialHash drawGrid;
      drawGrid.build(mesh.vertices(), h, current_N);

      for (int i = 0; i < current_N; i++) {
        auto neighbors = drawGrid.getNeighbors(mesh.vertices()[i]);
        int drawn_count = 0; 
        
        for (int j : neighbors) { 
          if (j <= i) continue; 
          
          Vec3f A = mesh.vertices()[i];
          Vec3f B = mesh.vertices()[j];
          float distance = (B - A).mag();

          if (distance < h && distance > 0.001f) {
            if (drawn_count >= connection_limit) break;
            drawn_count++; 
            
            Vec3f dirA = velocity[i];
            if (dirA.mag() < 0.001f) dirA = (B - A);
            dirA.normalize();

            Vec3f dirB = velocity[j];
            if (dirB.mag() < 0.001f) dirB = (B - A);
            dirB.normalize();

            Vec3f P0 = A;
            Vec3f P3 = B;
            Vec3f P1, P2;

            if (state().syncCurvyLines) {
                P1 = A + dirA * (distance * 0.35f);
                P2 = B - dirB * (distance * 0.35f);
            } else {
                P1 = A + (B - A) * 0.333f;
                P2 = A + (B - A) * 0.666f;
            }
            
            float dist_t = distance / h;
            float math_opacity = 0.8f * (1.0f - dist_t)+0.15f; 
            math_opacity = std::max(0.0f, std::min(math_opacity, 1.0f));
            
            float r_color = std::min(2.0f * dist_t, 1.0f);
            float g_color = std::min(2.0f * (1.0f - dist_t), 1.0f);
            Color c(r_color, g_color, 0.0f, math_opacity);

            Vec3f prev_v0, prev_v1;

            for (int k = 0; k <= segments; k++) {
              float t = (float)k / segments;
              float u = 1.0f - t;

              Vec3f p = (u*u*u)*P0 + 3*(u*u)*t*P1 + 3*u*(t*t)*P2 + (t*t*t)*P3;

              Vec3f tangent = 3*u*u*(P1 - P0) + 6*u*t*(P2 - P1) + 3*t*t*(P3 - P2);
              if (tangent.mag() < 0.001f) tangent = (B - A);
              tangent.normalize();

              Vec3f view_dir = (nav().pos() - p).normalize();
              Vec3f right = cross(tangent, view_dir);
              if (right.mag() < 0.001f) right = Vec3f(1, 0, 0); 
              right.normalize();

              Vec3f offset = right * line_thickness;
              Vec3f v0 = p + offset;
              Vec3f v1 = p - offset;
        
              if (k > 0) {
                float prev_t = (float)(k - 1) / segments;
                float curr_t = (float)k / segments;

                ribbons.vertex(prev_v0); ribbons.color(c); ribbons.texCoord(prev_t, -1.0f);
                ribbons.vertex(prev_v1); ribbons.color(c); ribbons.texCoord(prev_t,  1.0f);
                ribbons.vertex(v0);      ribbons.color(c); ribbons.texCoord(curr_t, -1.0f);
                
                ribbons.vertex(v0);      ribbons.color(c); ribbons.texCoord(curr_t, -1.0f);
                ribbons.vertex(prev_v1); ribbons.color(c); ribbons.texCoord(prev_t,  1.0f);
                ribbons.vertex(v1);      ribbons.color(c); ribbons.texCoord(curr_t,  1.0f);
              }
              prev_v0 = v0;
              prev_v1 = v1;
            }
          }
        }
      }

      g.blending(true);
      g.blendAdd(); 
      g.depthTesting(false);

      g.shader(ribbonShader);
      g.shader().uniform("lightIntensity", state().syncEnableBubbles ? 1.8f : 0.5f);
      g.shader().uniform("iTime", static_cast<float>(state().time));

      g.draw(ribbons); 
      g.blendTrans();
    }
    
    if (state().syncEnableBubbles) {
      g.shader(bubbleShader); 
      g.blending(true);
      g.blendTrans();    
      g.depthTesting(false); 
      g.cullFace(false);

      float b_size = state().syncBubbleSize;
      for (int i = 0; i < current_N; i++) {
        g.pushMatrix();
        g.translate(mesh.vertices()[i]);
        g.scale(b_size);
        Color c = mesh.colors()[i];
        g.shader().uniform("al_Color", c.r, c.g, c.b, 0.3f); 
        g.draw(sphereMesh); 
        g.popMatrix();
      }

    } else {
      g.blending(true);
      g.blendAdd();
      g.depthTesting(false);
      g.shader(pointShader);
      g.shader().uniform("pointSize", state().syncPointSize / 100.0f);
      
      vector<Vec3f> original_pos(MAX_N);
      for(int i = current_N; i < MAX_N; i++) {
          original_pos[i] = mesh.vertices()[i];
          mesh.vertices()[i].set(99999.0f, 99999.0f, 99999.0f);
      }
      
      g.draw(mesh);
      for(int i = current_N; i < MAX_N; i++) {
          mesh.vertices()[i] = original_pos[i];
      }
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
  if (!file.is_open()) {
    return "";
  }
  string returnValue = "";
  while (file.good()) {
    string line;
    getline(file, line);
    if (!line.empty() && line[line.length() - 1] == '\r') {
      line.erase(line.length() - 1);
    }
    returnValue += line + "\n";
  }
  return returnValue;
}