#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <thread>
#include "ctpl_stl.h"

class Particle {
    public:
        Particle(sf::Vector2f position, int index) : position(position), index(index) {}
        Particle() : position(sf::Vector2f()), index(0) {}
        sf::Vector2f position, predictedPosition, velocity, acceleration;
        float density, pressure, nearPressure;
        int index;
        bool isStatic = false;
};

const int SIMULATION = 0;
const int PLAYGROUND = 1;
const int AERODYNAMICS = 2;

class Simulation {
    public:
        Simulation(int particlesCount, float particleMass, float smoothingRadius, float targetDensity, float pressureMultiplier, float nearPressureMultiplier, float gravity, float viscosity, int simulationSteps);
        ~Simulation() { delete[] particles; }

        // Core functions
        void CreateParticles();
        void CalculateConstants();
        void Update();
        void Draw(sf::RenderTexture& target);
        void HandleInputs();
        void SetDeltatime(float dt) { deltaTime = dt; }
        void Reset();
        void Pause() { paused = !paused; }

        // Settings
        void SetStartingSize(float size) { if (size != spawnSquareSize && paused) { Simulation::Reset(); } spawnSquareSize = size; }
        void SetParticlesAmount(int amount);
        void SetSmoothingRadius(float radius);
        void SetSimulationSteps(int amount) { simulationSteps = amount; }
        void SetTargetDensity(float newTargetDensity) { targetDensity = newTargetDensity; }
        void SetPressureMultiplier(float newPressureMultiplier) { pressureMultiplier = newPressureMultiplier; }
        void SetNearPressureMultiplier(float newNearPressureMultiplier) { nearPressureMultiplier = newNearPressureMultiplier; }
        float& GetViscosity() { return viscosity; }
        float& GetTimeMultiplier() { return timeMultiplier; }
        float& GetParticlesRadius() { return particleRadius; }
        float& GetSpawnsPerSecond() { return spawnsPerSecond; }
        void SetGravity(float newGravity, bool doGravity) { gravity = newGravity; enableGravity = doGravity; }
        void SetInteractionAbility(bool enable) { enableMouse = enable; } // Enable or disable the users ability to draw on the screen
        void SetMode(int newMode) { mode = newMode; }
        void ToggleParticleSpawner() { spawnParticles = !spawnParticles; }
        int GetParticleCount() { return particlesCount; }

        // Other
        void SpawnParticle(Particle& particle);
        void AddWhiteParticle() { whiteParticles.push_back(particlesCount); }

        // Shapes (W.I.P)
        class Shape {
            public:
                Shape(sf::Vector2f size, int density) : size(size), density(density) {}
                Shape();
                virtual std::vector<sf::Vector2f> GetPositions() const { return {}; }
                void SetDensity(int newDensity) { density = newDensity; }
                sf::Vector2f size;
                int density;
        };
        class Circle : public Shape {
            public: 
                Circle(sf::Vector2f size, int density) : size(size), density(density) {}
                std::vector<sf::Vector2f> GetPositions() const override;
                sf::Vector2f size;
                int density;
        };
        class Rectangle : public Shape {
            public: 
                Rectangle(sf::Vector2f size, int density) : size(size), density(density) {}
                std::vector<sf::Vector2f> GetPositions() const override;
                sf::Vector2f size;
                int density;
        };
    private:
        // Main variables
        int particlesCount, initialParticlesCount, simulationSteps;
        float particleMass, smoothingRadius, targetDensity, pressureMultiplier, nearPressureMultiplier, gravity, viscosity, timeMultiplier = 1;
        float invSmoothingRadius;
        float smoothingRadius2, invSmoothingRadius2;
        int cells;
        bool paused = true;

        // Settings
        int mode = SIMULATION;
        bool enableGravity = true, enableMouse = true, spawnParticles = false;
        sf::Vector2f startingPosition = sf::Vector2f(1920 / 2, 1080 / 2);
        float spawnSquareSize = 1024;

        // Mouse drag info
        sf::Vector2f originalMousePos;
        sf::Vector2f originalPosition;
        bool mousePressed = false;

        // Rendering
        float particleRadius = 3;
        sf::VertexBuffer particleVertexes;
        sf::VertexBuffer circleVertexes;
        sf::Color GetParticleColor(int i);
        sf::Font font;
        std::vector<sf::Text> debugText;
        std::vector<int> whiteParticles;

        // Time
        sf::Clock clock2;
        sf::Clock renderClock;
        float lastDelay;
        const float FIXED_DT = 1.f / 120.f;
        float deltaTime, accumulator = 0.f;

        // Thread management
        ctpl::thread_pool threadPool;

        // Playground mode circle settings
        const float circleRadius = 10.f;
        std::vector<sf::Vector2f> circles;

        // Playground mode particle spawns
        float spawnRadius = 50;
        float timeElapsedSinceLastSpawn = 0.0f;
        float spawnsPerSecond = 50.f;
        const int MAX_PARTICLES = 50000;

        // Grid
        Particle *particles = nullptr;
        int horizontalCells, verticalCells;
        std::vector<std::vector<int>> grid;
        std::vector<std::vector<int>> circleGrid;
        int CellPosToKey(sf::Vector2i pos);
        sf::Vector2i PosToCellPos(sf::Vector2f pos);

        std::vector<int> GetNeighborCells(sf::Vector2i cellPos);

        // Important functions for calculations
        void PredictPositions();
        void CalculateParticleLookups();
        void ParallelCalculateDensities(int threadCount);
        void ParallelCalculatePressureViscosityForces(int threadCount);
        void ApplyMovements();
        void ResolveCollisions();
        sf::Vector2f CalculateParticleForces(Particle* particle);
        float GetParticleInfluence(int p1, int p2);
        sf::Vector2f CalculatePressureViscosityForce(Particle* particle);
        float DensityToPressure(float density);
        float NearDensityToNearPressure(float nearDensity);
        float CalculateSharedPressure(float pressure1, float pressure2);
        sf::Vector2f GetExternalForces(int particleIndex);

        void CalculateForces(int start, int end);
        void CalculateDensities(int start, int end);
        
        // Smoothing functions
        float smoothingKernelNormalization, poly6Normalization, spikyNormalization, spikyDerivativeNormalization, viscosityKernelNormalization, SpikyPow3Normalization, SpikyPow3DerivativeNormalization;
        float Poly6(float dst);
        float SpikyDerivative(float dst);
        float ViscosityKernel(float dst);
        float SpikyKernelPow3(float dst);
        float DerivativeSpikyPow3(float dst);

        // Lookups
        const int lookupSize = 1028;
        float Poly6Lookup(float dstSquared);
        float SpikyDerivativeLookup(float dst);
        float ViscosityKernelLookup(float dst);
        float SpikyKernelPow3Lookup(float dstSquared);
        float DerivativeSpikyPow3Lookup(float dst);

        std::vector<float> poly6Lookup;
        std::vector<float> spikyDerivativeLookup;
        std::vector<float> viscosityKernelLookup;
        std::vector<float> spikyKernelPow3Lookup;
        std::vector<float> derivativeSpikyPow3Lookup;
};