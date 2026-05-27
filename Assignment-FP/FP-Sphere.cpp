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
#include <iostream>

using namespace al;
using namespace std;

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}

string slurp(string fileName);  

const int MAX_N = 500; 

// SPATIAL HASH GRID
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

  // --- PARAMETER SYNCHRONIZATION DATA MODULE ---
  int syncActiveParticles;
  int syncMaxRibbons;
  float syncPointSize;
  float syncFocalDepth;
  float syncBubbleSize;

  bool syncEnableSpring;
  bool syncSpringToOrigin;
  bool syncEnableSwarm;
  bool syncEnableWarp;
  bool syncEnableRibbons;
  bool syncCurvyLines;
  bool syncEnableBubbles;
};

// --- KEYFRAME STRUCT WITH ALL PARAMETERS ---
struct Keyframe {
    string name;
    float duration;
    Vec3f pos;
    Quatf rot;
    
    // Sliders
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

    // Toggles
    float enableSpring;
    float springToOrigin;
    float enableSwarm;
    float enableWarp;
    float enableRibbons;
    float curvyLines;
    float enableBubbles;
};

class Timeline {
public:
    vector<Keyframe> events;
    int currentIndex = 0;
    float timer = 0.0f;
    bool active = false;

    // --- SAVE FUNCTION ---
    void save(const string& filename, const string& name, float dur, const Vec3f& p, const Quatf& r,
              int ap, int mr, float ps, float ts, float df, float ss, float sl, float fd, float pr, float rs, float dps, float v, float bs,
              float es, float sto, float esw, float ew, float er, float cl, float eb) {
        ofstream file(filename, ios::app);
        file << "{ name: " << name << ", duration: " << dur 
             << ", posX: " << p.x << ", posY: " << p.y << ", posZ: " << p.z 
             << ", rotW: " << r.w << ", rotX: " << r.x << ", rotY: " << r.y << ", rotZ: " << r.z 
             << ", activeParticles: " << ap << ", maxRibbons: " << mr << ", pointSize: " << ps 
             << ", timeStep: " << ts << ", dragFactor: " << df << ", springStiffness: " << ss 
             << ", springLength: " << sl << ", focalDepth: " << fd << ", perceptionRadius: " << pr 
             << ", repulsionStrength: " << rs << ", desiredPersonalSpace: " << dps << ", viscosity: " << v 
             << ", bubbleSize: " << bs << ", enableSpring: " << es << ", springToOrigin: " << sto 
             << ", enableSwarm: " << esw << ", enableWarp: " << ew << ", enableRibbons: " << er 
             << ", curvyLines: " << cl << ", enableBubbles: " << eb 
             << " }" << endl;
    }

    float getValue(string line, string key) {
        size_t pos = line.find(key + ":"); 
        if (pos == string::npos) return 0.0f;
        
        // Grab everything after the key prefix identifier
        string sub = line.substr(pos + key.length() + 1); 
        
        // Strip trailing commas, spaces, or closing curly braces safely
        size_t endPos = sub.find_first_of(",}");
        if (endPos != string::npos) {
            sub = sub.substr(0, endPos);
        }
        
        try {
            return stof(sub);
        } catch (...) {
            return 0.0f; // Safe fallback if string is malformed
        }
    }

    void load(const string& filename) {
        events.clear();
        ifstream file(filename);
        if (!file.is_open()) {
            std::cout << "⚠️ TIMELINE NOTICE: Could not open " << filename << " for reading." << std::endl;
            return;
        }
        string line;
        int count = 0;
        while (getline(file, line)) {
            if (line.find("{") != string::npos) {
                Keyframe k;
                k.duration = getValue(line, "duration");
                k.pos = Vec3f(getValue(line, "posX"), getValue(line, "posY"), getValue(line, "posZ"));
                
                // Parse rotation entries
                float w = getValue(line, "rotW");
                float x = getValue(line, "rotX");
                float y = getValue(line, "rotY");
                float z = getValue(line, "rotZ");
                
                // Fallback to identity rotation if data matrix collapses to zero
                if (w == 0.0f && x == 0.0f && y == 0.0f && z == 0.0f) {
                    w = 1.0f;
                }
                
                // CRITICAL FIX: Explicitly normalize quaternion to prevent SLERP jumping/stalling
                k.rot = Quatf(w, x, y, z).normalize();
                
                // Sliders
                k.activeParticles = (int)getValue(line, "activeParticles");
                k.maxRibbons = (int)getValue(line, "maxRibbons");
                k.pointSize = getValue(line, "pointSize");
                k.timeStep = getValue(line, "timeStep");
                k.dragFactor = getValue(line, "dragFactor");
                k.springStiffness = getValue(line, "springStiffness");
                k.springLength = getValue(line, "springLength");
                k.focalDepth = getValue(line, "focalDepth");
                k.perceptionRadius = getValue(line, "perceptionRadius");
                k.repulsionStrength = getValue(line, "repulsionStrength");
                k.desiredPersonalSpace = getValue(line, "desiredPersonalSpace");
                k.viscosity = getValue(line, "viscosity");
                k.bubbleSize = getValue(line, "bubbleSize");

                // Toggles
                k.enableSpring = getValue(line, "enableSpring");
                k.springToOrigin = getValue(line, "springToOrigin");
                k.enableSwarm = getValue(line, "enableSwarm");
                k.enableWarp = getValue(line, "enableWarp");
                k.enableRibbons = getValue(line, "enableRibbons");
                k.curvyLines = getValue(line, "curvyLines");
                k.enableBubbles = getValue(line, "enableBubbles");

                events.push_back(k);
                count++;
            }
        }
        std::cout << "✅ TIMELINE SUCCESS: Loaded " << count << " complete keyframe profiles from " << filename << std::endl;
    }
};

struct AlloApp : DistributedAppWithState<WorldState> {
  // GUI Parameters (Constructors explicitly declare slider min/max boundaries)
  ParameterInt activeParticles{"Active Particles", "", 100, 10, MAX_N};
  ParameterInt maxRibbons{"Max Ribbons", "", 10, 1, 25};

  Parameter pointSize{"Point Size", "", 4.5f, 1.0f, 10.0f};
  Parameter timeStep{"Time Step", "", 0.3f, 0.01f, 0.6f};
  Parameter dragFactor{"Drag Factor", "", 0.7f, 0.0f, 0.9f};

  Parameter springStiffness {"Spring Stiffness", "", 0.1f, 0.0f, 0.9f};
  Parameter springLength    {"Spring Length", "", 6.0f, 0.0f, 50.0f};
  Parameter focalDepth      {"Focal Depth", "", 10.0f, -10.0f, 100.0f};

  Parameter perceptionRadius    {"Perception Radius", "", 3.2f, 0.5f, 20.0f}; 
  Parameter repulsionStrength   {"Repulsion Strength", "", 42.0f, 1.0f, 100.0f};    
  Parameter desiredPersonalSpace{"Personal Space", "", 5.5f, 1.0f, 20.0f};     
  Parameter viscosity           {"Viscosity", "", 1.4f, 0.0f, 10.0f};

  Parameter bubbleSize          {"Bubble Size", "", 1.5f, 0.01f, 5.0f};
  
  ParameterBool enableSpring  {"Enable Spring", "", 1.0f};
  ParameterBool springToOrigin{"Spring Center to Origin", "", 0.0f};
  ParameterBool enableSwarm   {"Enable Swarm", "", 1.0f};
  ParameterBool enableWarp    {"Enable Warp", "", 1.0f};
  ParameterBool enableRibbons {"Enable Ribbons", "", 1.0f};
  ParameterBool curvyLines    {"Curvy Lines", "", 1.0f}; 
  ParameterBool enableBubbles {"Enable Bubbles", "", 1.0f};
  
  ParameterBool saveKeyframe{"Save Keyframe", "", 0.0f};

  // Advanced Timeline Actions & Telemetry
  ParameterBool playAutomation{"Play Automation", "Controls", 0.0f};
  ParameterBool pauseAutomation{"Pause Automation", "Controls", 0.0f};
  ParameterBool stopAutomation{"Stop Automation", "Controls", 0.0f};
  
  ParameterInt currentFrameNum{"Current Frame Index", "Status", 0, 0, 100};
  ParameterString timelineStatus{"Timeline Status", "Status", "Stopped"};
  ParameterVec3 cameraPosDisplay{"Camera Position (XYZ)", "Status", Vec3f(0.0f)};

  Parameter transitionSpeed{"Transition Speed Factor", "Controls", 1.0f, 0.1f, 10.0f};
  Parameter holdDuration{"Keyframe Hold Time (sec)", "Controls", 2.0f, 0.0f, 10.0f};
  ParameterString sequencingPhase{"Current Sequence Phase", "Status", "Idle"};

  ShaderProgram pointShader;
  ShaderProgram bubbleShader; 
  ShaderProgram ribbonShader;

  Mesh mesh;
  Mesh sphereMesh; 
  Timeline timeline;

  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;

  void onInit() override {
    if (isPrimary()) {
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();

        gui.add(activeParticles);
        gui.add(maxRibbons);
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
        gui.add(curvyLines); 
        gui.add(enableBubbles);
        gui.add(saveKeyframe);

        gui.add(playAutomation);
        gui.add(pauseAutomation);
        gui.add(stopAutomation);
        gui.add(transitionSpeed);  
        gui.add(holdDuration);     
        gui.add(currentFrameNum);
        gui.add(timelineStatus);
        gui.add(sequencingPhase);  
        gui.add(cameraPosDisplay);
        
        timeline.load("../events.txt");
    }

    parameterServer() << pointSize << timeStep << dragFactor << springStiffness 
                      << springLength << focalDepth << perceptionRadius 
                      << repulsionStrength << desiredPersonalSpace << viscosity 
                      << bubbleSize << enableSpring << springToOrigin 
                      << enableSwarm << enableWarp << enableRibbons 
                      << enableBubbles << saveKeyframe
                      << playAutomation << pauseAutomation << stopAutomation
                      << transitionSpeed << holdDuration;
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

    nav().pos(0, 0, 10);
  }
  
  bool freeze = false;
  bool isHolding = false; 

  void onAnimate(double dt) override {
    if (!isPrimary()) return;
    
    state().frame++;
    state().time += dt;

    if (freeze) return;

    cameraPosDisplay.set(nav().pos());

    // --- 1. PLAYBACK CONTROLLERS ---
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
        
        if (!timeline.events.empty()) {
            Keyframe& first = timeline.events[0];
            nav().pos().set(first.pos);
            nav().quat().set(first.rot);
            activeParticles.set(first.activeParticles);
            maxRibbons.set(first.maxRibbons);
            pointSize.set(first.pointSize);
            timeStep.set(first.timeStep);
            dragFactor.set(first.dragFactor);
            springStiffness.set(first.springStiffness);
            springLength.set(first.springLength);
            focalDepth.set(first.focalDepth);
            perceptionRadius.set(first.perceptionRadius);
            repulsionStrength.set(first.repulsionStrength);
            desiredPersonalSpace.set(first.desiredPersonalSpace);
            viscosity.set(first.viscosity);
            bubbleSize.set(first.bubbleSize);
            enableSpring.set(first.enableSpring);
            springToOrigin.set(first.springToOrigin);
            enableSwarm.set(first.enableSwarm);
            enableWarp.set(first.enableWarp);
            enableRibbons.set(first.enableRibbons);
            curvyLines.set(first.curvyLines);
            enableBubbles.set(first.enableBubbles);
        }
        stopAutomation.set(0); 
    }

    // --- 2. ADVANCED TWO-PHASE INTERPOLATION TIMELINE ---
    if (timeline.active && !timeline.events.empty()) {
        Keyframe& curr = timeline.events[timeline.currentIndex];
        Keyframe& next = timeline.events[(timeline.currentIndex + 1) % timeline.events.size()];
        
        currentFrameNum.set(timeline.currentIndex);

        // PHASE A: Hold State (Only locks camera position and view configurations)
        if (isHolding) {
            sequencingPhase.set("Holding Scene");
            
            timeline.timer += dt; 
            
            nav().pos().set(curr.pos);
            nav().quat().set(curr.rot);
            
            activeParticles.set(curr.activeParticles);
            maxRibbons.set(curr.maxRibbons);
            pointSize.set(curr.pointSize);
            focalDepth.set(curr.focalDepth);
            bubbleSize.set(curr.bubbleSize);
            
            enableSpring.set(curr.enableSpring);
            springToOrigin.set(curr.springToOrigin);
            enableSwarm.set(curr.enableSwarm);
            enableWarp.set(curr.enableWarp);
            enableRibbons.set(curr.enableRibbons);
            curvyLines.set(curr.curvyLines);
            enableBubbles.set(curr.enableBubbles);

            if (timeline.timer >= holdDuration.get()) {
                isHolding = false;     
                timeline.timer = 0.0f; 
            }
        }
        // PHASE B: Transition State (Interpolation Engine)
        else {
            sequencingPhase.set("Interpolating Target");

            timeline.timer += dt; 

            float totalTransitionTime = curr.duration * transitionSpeed.get();
            if (totalTransitionTime < 0.01f) totalTransitionTime = 0.01f; 

            float t = timeline.timer / totalTransitionTime;
            if (t > 1.0f) t = 1.0f; 

            // FIX: Enforce strict, isolated linear interpolation between raw keyframe parameters
            Vec3f blendPos = curr.pos + (next.pos - curr.pos) * t;
            Quatf blendRot = Quatf::slerp(curr.rot, next.rot, t);

            // Directly override the camera matrix state to match the pristine timeline trajectory
            nav().pos().set(blendPos);
            nav().quat().set(blendRot);

            // Interpolating view properties smoothly (Type-safe synchronization for ImGui)
            activeParticles.set(static_cast<int>(curr.activeParticles + (next.activeParticles - curr.activeParticles) * t));
            maxRibbons.set(static_cast<int>(curr.maxRibbons + (next.maxRibbons - curr.maxRibbons) * t));
            pointSize.set(curr.pointSize + (next.pointSize - curr.pointSize) * t);
            focalDepth.set(curr.focalDepth + (next.focalDepth - curr.focalDepth) * t);
            bubbleSize.set(curr.bubbleSize + (next.bubbleSize - curr.bubbleSize) * t);

            // Keep physical simulation constants snapping instantly at boundaries
            timeStep.set(next.timeStep);
            dragFactor.set(next.dragFactor);
            springStiffness.set(next.springStiffness);
            springLength.set(next.springLength);
            perceptionRadius.set(next.perceptionRadius);
            repulsionStrength.set(next.repulsionStrength);
            desiredPersonalSpace.set(next.desiredPersonalSpace);
            viscosity.set(next.viscosity);

            enableSpring.set(t < 0.5f ? curr.enableSpring : next.enableSpring);
            springToOrigin.set(t < 0.5f ? curr.springToOrigin : next.springToOrigin);
            enableSwarm.set(t < 0.5f ? curr.enableSwarm : next.enableSwarm);
            enableWarp.set(t < 0.5f ? curr.enableWarp : next.enableWarp);
            enableRibbons.set(t < 0.5f ? curr.enableRibbons : next.enableRibbons);
            curvyLines.set(t < 0.5f ? curr.curvyLines : next.curvyLines);
            enableBubbles.set(t < 0.5f ? curr.enableBubbles : next.enableBubbles);

            if (t >= 1.0f) {
                timeline.currentIndex = (timeline.currentIndex + 1) % timeline.events.size();
                timeline.timer = 0.0f;
                isHolding = true; 
            }
        }
    }

    Vec3f camPos(nav().pos());
    Vec3f ur(nav().ur()); 
    Vec3f uu(nav().uu()); 
    Vec3f uf(nav().uf());

    int current_N = activeParticles.get();

    // --- 3. PHYSICS ENGINE MODULES ---
    if (enableSpring.get() == 1.0f) {
      Vec3f focal_point = (springToOrigin.get() == 1.0f) ? Vec3f(0.0f, 0.0f, 0.0f) : camPos + (uf * focalDepth.get());  

      for (int i = 0; i < current_N; i++) {
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

          if (r < radius && r > 0.0001f) {
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

    // --- 4. NUMERICAL INTEGRATION & CLUSTER STATE COPIES ---
    vector<Vec3f> &position(mesh.vertices());
    for (int i = 0; i < current_N; i++) {
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

    if (enableBubbles.get() == 1.0f) {
        float min_dist = bubbleSize.get() * 2.0f;
        for (int iter = 0; iter < 3; iter++) {
            for (int i = 0; i < current_N; i++) {
                for (int j = i + 1; j < current_N; j++) {
                    Vec3f dir = position[i] - position[j];
                    float dist = dir.mag();
                    if (dist < min_dist && dist > 0.0001f) {
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

    // --- 5. SNAPSHOT KEYFRAME RECORDER ---
    if (saveKeyframe.get()) {
        timeline.save("../events.txt", "Event", 5.0, nav().pos(), nav().quat(),
                      activeParticles.get(), maxRibbons.get(), pointSize.get(), timeStep.get(), dragFactor.get(),
                      springStiffness.get(), springLength.get(), focalDepth.get(), perceptionRadius.get(),
                      repulsionStrength.get(), desiredPersonalSpace.get(), viscosity.get(), bubbleSize.get(),
                      enableSpring.get(), springToOrigin.get(), enableSwarm.get(), enableWarp.get(),
                      enableRibbons.get(), curvyLines.get(), enableBubbles.get());
                      
        cout << "Snapshot saved cleanly to repository root level events.txt" << endl;
        
        timeline.load("../events.txt");
        saveKeyframe.set(0); 
    }

    // --- 6. CLUSTER STATE FINALIZATION WITH MEMORY PACKING ---
    state().syncActiveParticles = activeParticles.get();
    state().syncMaxRibbons      = maxRibbons.get();
    state().syncPointSize       = pointSize.get();
    state().syncFocalDepth      = focalDepth.get();
    state().syncBubbleSize      = bubbleSize.get();
    
    state().syncEnableSpring   = (enableSpring.get() == 1.0f);
    state().syncSpringToOrigin = (springToOrigin.get() == 1.0f);
    state().syncEnableSwarm    = (enableSwarm.get() == 1.0f);
    state().syncEnableWarp     = (enableWarp.get() == 1.0f);
    state().syncEnableRibbons  = (enableRibbons.get() == 1.0f);
    state().syncCurvyLines     = (curvyLines.get() == 1.0f);
    state().syncEnableBubbles  = (enableBubbles.get() == 1.0f);

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

    // CHANNELS DIRECTLY LEVERAGE THE GUARANTEED STATE PACKETS
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

          if (distance < h && distance > 0.0001f) {
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
            math_opacity = std::max(0.0f, std::min(math_opacity, 1.0f));//clamping the value
            
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

      g.shader(ribbonShader);
      g.shader().uniform("lightIntensity", state().syncEnableBubbles ? 1.8f : 0.5f);
      g.shader().uniform("iTime", static_cast<float>(state().time));

      g.draw(ribbons); 
      g.blendAdd();
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
    std::cout << "\n🚨 ERROR: slurp() COULD NOT FIND OR OPEN FILE: " << fileName << std::endl;
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