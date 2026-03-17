#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <thread>
#include "ctpl_stl.h"

class Vec2 {
    public:
        Vec2(float x, float y) : x(x), y(y) {}
        Vec2(sf::Vector2f v) : x(v.x), y(v.y) {}
        Vec2(sf::Vector2i v) : x(v.x), y(v.y) {}
        Vec2() : x(0), y(0) {}
        float x, y;
        float dot(const Vec2& b) const { return x*b.x + y*b.y; }
        float length() const { return std::sqrt(Vec2::dot(*this)); }
        float lengthSquared() const { return Vec2::dot(*this); }
        Vec2 operator+(const Vec2& right) const { return Vec2(x+right.x, y+right.y); }
        Vec2 operator-(const Vec2& right) const { return Vec2(x-right.x, y-right.y); }
        Vec2 operator/(float right) const { float inv = 1.f / right; return Vec2(x*inv, y*inv); }
        Vec2 operator*(float right) const { return Vec2(x*right, y*right); }
        Vec2 operator*(const Vec2& right) const { return Vec2(x*right.x, y*right.y); }
        Vec2& operator+=(const Vec2& right) { x += right.x; y += right.y; return *this; }
        Vec2& operator-=(const Vec2& right) { x -= right.x; y -= right.y; return *this; }
        Vec2& operator*=(const Vec2& right) { x *= right.x; y *= right.y; return *this; }
        Vec2& operator*=(const float right) { x *= right; y *= right; return *this; }
        Vec2 operator-() const { return Vec2(-x, -y); }
        operator sf::Vector2f() const { return sf::Vector2f(x, y); }
};

inline Vec2 operator*(float s, const Vec2& v) {
    return v * s;
}

class Particle {
    public:
        Particle(Vec2 position, int index) : position(position), index(index) {}
        Particle() : position(Vec2()), index(0) {}
        Vec2 position, predictedPosition, velocity, acceleration;
        float density, pressure;
        int index;
};

const int SIMULATION = 0;
const int PLAYGROUND = 1;

class Simulation {
    public:
        Simulation(int particlesCount, float particleMass, float smoothingRadius, float targetDensity, float pressureMultiplier, float gravity, float viscosity, int simulationSteps);
        ~Simulation() { delete[] particles; }
        void Update();
        void Draw(sf::RenderTexture& target);
        void HandleInputs();
        float DensityAtPoint(Vec2 pos);
        float GetSmoothingRadius() const { return smoothingRadius; }
        void SetDeltatime(float dt) { deltaTime = dt; }
        void Reset();

        void SetTargetDensity(float newTargetDensity) { targetDensity = newTargetDensity; }
        void SetPressureMultiplier(float newPressureMultiplier) { pressureMultiplier = newPressureMultiplier; }
        void SetViscosity(float newViscosity) { viscosity = newViscosity; }
        void SetGravity(float newGravity, bool doGravity) { gravity = newGravity; enableGravity = doGravity; }
        void SetTimeMultiplier(float newTimeMultiplier) { timeMultiplier = newTimeMultiplier; }
        void SetKernelLookup(bool enable) { useKernelLookups = enable; }
        void SetInteractionAbility(bool enable) { enableMouse = enable; } // Enable or disable the users ability to draw on the screen
        void SetMode(bool newMode) { mode = newMode; }
        void EnableParticleSpawner(bool enable) { spawnParticles = enable; }
    private:
        int particlesCount, initialParticlesCount, simulationSteps;
        float particleMass, smoothingRadius, targetDensity, pressureMultiplier, gravity, viscosity, timeMultiplier = 1;
        float invSmoothingRadius;
        float smoothingRadius2, invSmoothingRadius2;
        int cells;

        int mode = SIMULATION;

        bool enableGravity = true, useKernelLookups = true, enableMouse = true, spawnParticles = false;

        sf::VertexBuffer particleVertexes;
        sf::VertexBuffer circleVertexes;

        sf::Clock clock2;
        sf::Clock renderClock;
        float lastDelay;
        const float FIXED_DT = 1.f / 120.f;
        float deltaTime, accumulator = 0.f;

        ctpl::thread_pool threadPool;

        const float circleRadius = 10.f;
        std::vector<Vec2> circles;

        float spawnRadius = 50; // The spawn radius where particles will spawn in the playground mode

        Particle *particles = nullptr;
        int horizontalCells, verticalCells;
        std::vector<std::vector<int>> grid;
        std::vector<std::vector<int>> circleGrid;

        void SpawnParticle(Particle& particle);
        const int MAX_PARTICLES = 10000;

        float timeElapsedSinceLastSpawn = 0.0f;
        float spawnsPerSecond = 500.f;

        std::vector<int> GetNeighborCells(sf::Vector2i cellPos);

        void PredictPositions();
        void CalculateParticleLookups();
        void CalculateDensities(int threadCount);
        void CalculatePressureViscosityForces(int threadCount);
        void ApplyMovements();
        void ResolveCollisions();
        Vec2 CalculateParticleForces(Particle* particle);

        void ParallelCalculateForces(int start, int end);
        void ParallelCalculateDensities(int start, int end);
        
        float Poly6(float dst);
        float SpikyDerivative(float dst);
        float ViscosityKernel(float dst);

        const int lookupSize = 1028;

        float Poly6Lookup(float dstSquared);
        float SpikyDerivativeLookup(float dst);
        float ViscosityKernelLookup(float dst);

        std::vector<float> poly6Lookup;
        std::vector<float> spikyDerivativeLookup;
        std::vector<float> viscosityKernelLookup;

        float GetParticleInfluence(int p1, int p2);
        Vec2 CalculatePressureViscosityForce(Particle* particle);
        float DensityToPressure(float density);
        float CalculateSharedPressure(float pressure1, float pressure2);
        Vec2 GetInteractionForce(int particleIndex);

        int CellPosToKey(sf::Vector2i pos);
        sf::Vector2i PosToCellPos(Vec2 pos);

        sf::Color GetParticleColor(int i);

        sf::Font font;
        std::vector<sf::Text> debugText;

        float smoothingKernelNormalization, poly6Normalization, spikyNormalization, spikyDerivativeNormalization, viscosityKernelNormalization;
};