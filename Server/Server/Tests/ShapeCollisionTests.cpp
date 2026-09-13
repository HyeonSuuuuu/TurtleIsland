#include "pch.h"
#include "ShapeCollisionMap.h"
#include "ObstacleMap.h"
#include "../../../Common/ThirdParty/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <atomic>

using Json = nlohmann::json;
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

static Json Scene()
{
    return {{"schema", 1}, {"units", "cm"}, {"complete", true}, {"errors", Json::array()},
        {"source_level", "/Test/Collision"},
        {"spawn", {{"x", -250}, {"y", -250}, {"z", 96}, {"yaw", 0}, {"ground_z", 0}}},
        {"terrain", {{"cols", 10}, {"rows", 10}, {"cell", 100}, {"origin", {-500, -500}},
            {"blocked", std::vector<std::string>(10, std::string(10, '0'))}}},
        {"polygons", Json::array({{{"source", "fence"}, {"vertices", {{-5,-200},{5,-200},{5,200},{-5,200}}}}})}};
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--validate")
    {
        ShapeCollisionMap map;
        if (!map.Load(argv[2])) { std::println("FAIL: {}", map.LastError()); return 1; }
        std::println("PASS: scene polygons={} PlayerStart=({},{})",map.PolygonCount(),map.SpawnX(),map.SpawnY());
        return 0;
    }
    const auto folder = std::filesystem::temp_directory_path() / ("TurtleIslandShapes-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "scene.json";
    auto write = [&](const Json& value) { std::ofstream file(path); file << value.dump(); };
    try
    {
        auto scene = Scene(); write(scene);
        ShapeCollisionMap map;
        const bool loaded = map.Load(path.string().c_str());
        Check(loaded, map.LastError().c_str());
        Check(map.CanTraverse(-100,0,-47.2f,0,42), "capsule can approach thin fence to its physical radius");
        Check(!map.CanTraverse(-100,0,-46.8f,0,42), "capsule overlap rejected");
        Check(map.CanTraverse(-48,-180,-48,180,42), "movement parallel to fence remains possible");
        Check(map.CanTraverse(-48,0,-100,0,42), "moving away from fence remains possible");
        Check(!map.CanTraverse(-400,0,400,0,42), "whole sweep blocks high-speed tunneling");
        Check(!map.CanTraverse(-400,0,400,0,0), "line of sight cannot pass through fence");
        Check(!map.CanTraverse(0,0,0,0,0), "point inside polygon blocked");
        Check(!map.CanTraverse(-30,0,-100,0,42), "initial overlap cannot bypass sweep");
        Check(map.CanTraverse(-36,-236,-36,-236,42), "rounded capsule clears outside corner without square inflation");
        Check(!map.CanTraverse(-30,-225,-30,-225,42), "rounded capsule still blocks corner overlap");
        Check(!map.CanTraverse(450,450,470,450,42), "map boundary includes capsule radius");
        Check(!map.CanTraverse(NAN,0,0,0,42), "NaN movement rejected");
        Check(!map.CanTraverse(-100,0,-100,0,-1), "negative radius rejected");
        float x = 0, y = 0;
        Check(map.SnapToFree(x,y,42) && map.CanTraverse(x,y,x,y,42), "spawn correction finds capsule-safe point");
        std::atomic<bool> valid = true;
        std::vector<std::thread> threads;
        for (int n=0;n<4;++n) threads.emplace_back([&] { for (int i=0;i<2000;++i)
            if (!map.CanTraverse(-100,0,-48,0,42) || map.CanTraverse(-100,0,100,0,42)) valid=false; });
        for (auto& thread : threads) thread.join();
        Check(valid, "shared immutable geometry supports independent concurrent queries");
        std::println("PASS: thin fence, continuous sweep, endpoint overlap, round corners, boundary, parallel reads");

        scene["terrain"]["blocked"][8] = "0000000010"; write(scene);
        Check(map.Load(path.string().c_str()), "terrain fixture loaded");
        Check(!map.CanTraverse(250,350,450,350,0), "terrain hole blocks path even with free endpoints");
        Check(map.CanTraverse(264,264,264,264,42), "terrain corner uses round capsule, not expanded square");

        scene=Scene();
        for (auto& vertex : scene["polygons"][0]["vertices"])
        {
            const double vx=vertex[0], vy=vertex[1];
            vertex={ (vx-vy)*std::sqrt(0.5), (vx+vy)*std::sqrt(0.5) };
        }
        write(scene); Check(map.Load(path.string().c_str()), "rotated fence loads");
        Check(!map.CanTraverse(-150,-150,150,150,42), "rotated fence sweep blocks crossing");
        Check(map.CanTraverse(-200,250,200,250,42), "rotated fence does not become a large AABB blocker");

        scene=Scene();
        scene["polygons"][0]["vertices"]={{-5,-200},{4.5,-200},{5,-199.5},{5,200},{-5,200}};
        write(scene); Check(map.Load(path.string().c_str()), "sub-centimetre convex bevel is preserved");
        Check(!map.CanTraverse(-100,0,100,0,42), "beveled shape still blocks full sweep");
        Check(!map.CanTraverse(4.9f,-199.4f,4.9f,-199.4f,0), "original bevel vertices remain covered");
        Check(map.CanTraverse(-100,0,-47.2f,0,42), "bevel does not introduce extra clearance padding");
        Check(!map.CanTraverse(-46.8f,0,-46.8f,0,42), "endpoint overlap is still blocked");
        scene["polygons"][0]["vertices"]={{-25,0},{0,0},{0,1.68}};
        write(scene); Check(map.Load(path.string().c_str()), "thin convex fragment does not collapse during welding");
        Check(!map.CanTraverse(-0.1f,0.1f,-0.1f,0.1f,0), "thin fragment interior is blocked");

        for (int test=0;test<9;++test)
        {
            scene=Scene();
            if(test==0) scene["complete"]=false;
            if(test==1) scene["errors"]={"unsupported collider"};
            if(test==2) scene["schema"]=2;
            if(test==3) scene["terrain"]["blocked"][0]="invalid";
            if(test==4) scene["polygons"][0]["vertices"]={{0,0},{100,0},{50,25},{100,100},{0,100}};
            if(test==5) scene["polygons"][0]["vertices"][0][0]=nullptr;
            if(test==6) scene["polygons"][0]["vertices"]={{0,0},{100,0},{100,100},{0,100},{0.5,50}};
            if(test==7) scene["polygons"][0]["vertices"]={{0,0},{100,100},{0,100},{100,0}};
            if(test==8) scene["polygons"][0]["vertices"]={{0,0},{50,0},{100,0}};
            write(scene); Check(!map.Load(path.string().c_str()) && !map.CanTraverse(-100,0,-100,0,42), "bad export fails closed");
        }
        scene=Scene(); scene["spawn"]["ground_z"]=12.5; scene["spawn"]["z"]=2000; write(scene);
        const auto config = folder / "settings.json";
        { std::ofstream file(config); file << Json{{"mode","shapes"},{"scene",path.string()}}.dump(); }
        ObstacleMap configured;
        Check(configured.LoadConfigured(config.string().c_str()), "shape backend selected");
        Check(MapSpawn::PLAYER_GROUND_Z == 12.5f, "respawn uses exported ground height, not elevated PlayerStart Z");
        Check(configured.CanTraverse(-100,0,-48,0,42), "shape mode does not AND the legacy grid");
        Check(!configured.CanTraverse(-100,0,100,0,42), "adapter enforces polygon collision");
        { std::ofstream file(folder / "PlayerSpawn.json"); file << scene["spawn"].dump(); }
        { std::ofstream file(folder / "map.dat");
          file << "10 10 100 -500 -500\n";
          for (int row=0;row<10;++row) file << "0000110000\n"; }
        { std::ofstream file(config); file << Json{{"mode","shadow"},{"scene","scene.json"}}.dump(); }
        Check(configured.LoadConfigured(config.string().c_str()), "relative files resolved beside config in shadow mode");
        Check(!configured.CanTraverse(-250,0,-48,0,42), "shadow keeps legacy rejection even where precise scene allows movement");
        { std::ofstream file(config); file << Json{{"mode","grid"},{"scene","absent.json"}}.dump(); }
        Check(configured.LoadConfigured(config.string().c_str()), "grid rollback does not require a shape export");
        Check(MapSpawn::PLAYER_GROUND_Z == 12.5f, "grid fallback retains exported ground height");
        { std::ofstream file(config); file << R"({"mode":"typo"})"; }
        Check(!configured.LoadConfigured(config.string().c_str()) && !configured.CanMoveTo(-100,0), "invalid mode fails closed");
        std::println("PASS: terrain boundary, rotated polygons, export validation and runtime mode selection");
        if (argc > 1)
        {
            const bool realLoaded = map.Load(argv[1]);
            Check(realLoaded, map.LastError().c_str());
            x=map.SpawnX(); y=map.SpawnY();
            Check(map.SnapToFree(x,y,42), "real map has safe spawn");
            std::println("PASS: real scene polygons={} spawn=({},{})", map.PolygonCount(),x,y);
        }
        std::filesystem::remove(path);
        std::filesystem::remove(config);
        std::filesystem::remove(folder / "PlayerSpawn.json");
        std::filesystem::remove(folder / "map.dat");
        std::filesystem::remove(folder);
        return 0;
    }
    catch (const std::exception& e) { std::println("FAIL: {}",e.what()); return 1; }
}
