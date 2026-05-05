#include "simulation.h"
#include <SFML/Graphics.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <execution>
#include <ranges>
#include <numeric>
#include <thread>
#include <future>
#include "ctpl_stl.h" // Threadpool library

#define PI 3.14159f

Simulation::Simulation(int particlesCount, float particleMass, float smoothingRadius, float targetDensity, float pressureMultiplier, float nearPressureMultiplier, float gravity, float viscosity, int simulationSteps) : particlesCount(particlesCount), initialParticlesCount(particlesCount),
        particleMass(particleMass), smoothingRadius(smoothingRadius), targetDensity(targetDensity), pressureMultiplier(pressureMultiplier), nearPressureMultiplier(nearPressureMultiplier), gravity(gravity), viscosity(viscosity), simulationSteps(simulationSteps), threadPool(std::thread::hardware_concurrency())
{
    std::cout << "Threads: " << threadPool.size() << std::endl;
    Simulation::CreateParticles();

    Simulation::CalculateConstants();

    particleVertexes = sf::VertexBuffer(sf::PrimitiveType::Triangles, sf::VertexBuffer::Usage::Stream);
    circleVertexes = sf::VertexBuffer(sf::PrimitiveType::Triangles, sf::VertexBuffer::Usage::Dynamic);
    std::cout << "Loaded Vertex Buffer\n";

    if (!font.openFromFile("Fonts/OpenSans-Bold.ttf")) std::cerr << "Couldn't load font file\n";
    int characterSize = 16;
    debugText.resize(3, sf::Text(font, "default text", characterSize));
    for (int i = 0; i < 3; i++) {
        sf::Text newText(font, "default text", characterSize);
        newText.setFillColor(sf::Color::White);
        newText.setPosition(sf::Vector2f(10, std::round(i*characterSize*2+50)));
        debugText.at(i) = newText;
    }
}

void Simulation::Reset(){
    paused = true;
    Simulation::CreateParticles();
    Simulation::CalculateConstants();

    circleGrid.clear();
    circleGrid.resize(horizontalCells * verticalCells);
    circles.clear();
}

void Simulation::CalculateConstants(){
    invSmoothingRadius = 1.f / smoothingRadius;
    smoothingRadius2 = smoothingRadius * smoothingRadius;
    invSmoothingRadius2 = 1.f / smoothingRadius2;

    cells = std::ceil(1920 / smoothingRadius) * std::ceil(1080 / smoothingRadius);
    smoothingKernelNormalization = 40.f / (7 * PI * smoothingRadius * smoothingRadius);
    poly6Normalization = (4.f / (PI * pow(smoothingRadius, 8)));
    spikyNormalization = (10.f / (PI * pow(smoothingRadius, 5)));
    spikyDerivativeNormalization = -30.f / (PI * pow(smoothingRadius, 5));
    viscosityKernelNormalization = (PI * pow(smoothingRadius, 8)) / 4;
    SpikyPow3Normalization = 10 / (PI * pow(smoothingRadius, 5));
    SpikyPow3DerivativeNormalization = 30 / (pow(smoothingRadius, 5) * PI);

    poly6Lookup.resize(lookupSize+1, 0.f);
    spikyDerivativeLookup.resize(lookupSize+1, 0.f);
    viscosityKernelLookup.resize(lookupSize+1, 0.f);
    spikyKernelPow3Lookup.resize(lookupSize+1, 0.f);
    derivativeSpikyPow3Lookup.resize(lookupSize+1, 0.f);
    float lookupSizeFloat = lookupSize;
    for (int i = 0; i < lookupSize+1; i++){
        float iFloat = i;
        float dstSquared = iFloat / lookupSizeFloat;
        float dst = std::sqrt(dstSquared) * smoothingRadius;
        poly6Lookup.at(i) = Simulation::Poly6(dst);
        spikyDerivativeLookup.at(i) = Simulation::SpikyDerivative(dstSquared * smoothingRadius);
        viscosityKernelLookup.at(i) = Simulation::ViscosityKernel(dstSquared * smoothingRadius);
        spikyKernelPow3Lookup.at(i) = Simulation::SpikyKernelPow3(dst);
        derivativeSpikyPow3Lookup.at(i) = Simulation::DerivativeSpikyPow3(dstSquared * smoothingRadius);
    }

    horizontalCells = std::ceil(1920.f / smoothingRadius);
    verticalCells = std::ceil(1080.f / smoothingRadius);
    
    circleGrid.clear();
    circleGrid.resize(horizontalCells * verticalCells);

    for (int i = 0; i < circles.size(); i++) {
        sf::Vector2f& circle = circles.at(i);
        sf::Vector2i cellPos = Simulation::PosToCellPos(circle);
        circleGrid[cellPos.y * horizontalCells + cellPos.x].push_back(i);
    }
}

void Simulation::CreateParticles(){
    delete[] particles;
    particles = new Particle[MAX_PARTICLES];
    if (mode == SIMULATION){
        bool spawnParticlesInSquareShape = true;
        if (spawnParticlesInSquareShape){
            const sf::Vector2f topLeft = startingPosition - sf::Vector2f(spawnSquareSize / 2, spawnSquareSize / 2);
            int grid = std::ceil(std::sqrt(particlesCount));
            const float stepSize = spawnSquareSize / grid;
            int i = 0;
            for (int x = 0; x < grid && i < particlesCount; x++){
                for (int y = 0; y < grid && i < particlesCount; y++){
                    sf::Vector2f pos = topLeft + sf::Vector2f(y * stepSize, x * stepSize);
                    Particle p(pos, i);
                    particles[i] = p;
                    i++;
                }
            }
        }
        else {
            srand(time(NULL));
            for (int i = 0; i < particlesCount; i++){
                sf::Vector2f randomPos(rand() % 1920, rand() % 1080);
                Particle p(randomPos, i);
                particles[i] = p;
            }
        }
    } else if (mode == PLAYGROUND || mode == AERODYNAMICS){
        particlesCount = 0;
    }
}

void Simulation::SetParticlesAmount(int amount) {
    if (amount == particlesCount || !paused) return; // Only update if the particles count changed and if the simulation is paused
    particlesCount = amount;
    Simulation::CreateParticles();
}

void Simulation::SetSmoothingRadius(float radius) {
    if (radius == smoothingRadius) return; // Only update if the smoothing radius changed
    smoothingRadius = radius;
    Simulation::CalculateConstants();
}

void Simulation::SpawnParticle(Particle& particle){
    if (particlesCount+1 == MAX_PARTICLES) {
        std::cerr << "Error: Max particles reached: " << MAX_PARTICLES << std::endl;
        spawnParticles = false;
        return;
    }
    
    particles[particlesCount] = particle;
    particlesCount++;
}

int Simulation::CellPosToKey(sf::Vector2i cellPos){
    int hash = cellPos.x * 36523 + cellPos.y * 74923;
    int key = hash % cells;
    return abs(key);
}

sf::Vector2i Simulation::PosToCellPos(sf::Vector2f pos){
    int cellX = std::clamp((int)(pos.x * invSmoothingRadius), 0, horizontalCells - 1);
    int cellY = std::clamp((int)(pos.y * invSmoothingRadius), 0, verticalCells - 1);
    return sf::Vector2i(cellX, cellY);
}

std::vector<int> Simulation::GetNeighborCells(sf::Vector2i cellPos) {
    std::vector<int> neighbors;

    for (int dy = -1; dy <= 1; ++dy) {
        int ny = cellPos.y + dy;
        if (ny < 0 || ny >= verticalCells) continue;

        for (int dx = -1; dx <= 1; ++dx) {
            int nx = cellPos.x + dx;
            if (nx < 0 || nx >= horizontalCells) continue;

            int index = ny * horizontalCells + nx;
            if (index > -1 && index < horizontalCells * verticalCells && grid[index].size() != 0) neighbors.push_back(index);
        }
    }
    return neighbors;
}

void Simulation::CalculateParticleLookups(){
    grid.clear();
    grid.resize(horizontalCells * verticalCells);

    for (int i = 0; i < particlesCount; i++){
        Particle& p = particles[i];
        sf::Vector2i cellPos = Simulation::PosToCellPos(p.position);
        grid[cellPos.y * horizontalCells + cellPos.x].push_back(i);
    }
}

float Simulation::Poly6(float dst){
    dst = abs(dst);
    float q = dst / smoothingRadius;
    if (q <= 0.5f && q >= 0.f){
        return (6 * (q*q*q - q*q) + 1) * smoothingKernelNormalization;
    } else if (q > 0.5f && q <= 1.f){
        float x = 1 - q;
        return 2 * x*x*x * smoothingKernelNormalization;
    } else return 0;
}

float Simulation::SpikyDerivative(float dst){
    float x = smoothingRadius - dst;
    return spikyDerivativeNormalization * x * x;

    #ifdef OTHER_SPIKY_DERIVATIVE
    const float SpikyPow2DerivativeScalingFactor = 12.f / (pow(smoothingRadius, 4) * PI);
    if (dst <= smoothingRadius)
	{
		float v = smoothingRadius - dst;
		return -v * SpikyPow2DerivativeScalingFactor;
	}
	return 0;

    dst = abs(dst);
    float q = dst / smoothingRadius;
    if (q <= 0.5f && q >= 0.f){
        return -(18 * q * q - 12 * q) * (smoothingKernelNormalization / smoothingRadius);
    } else if (q > 0.5f && q <= 1.f){
        float x = 1 - q;
        return -6 * x*x * (smoothingKernelNormalization / smoothingRadius);
    } else return 0;
    #endif
}

float Simulation::SpikyKernelPow3(float dst)
{
	if (dst < smoothingRadius)
	{
		float v = smoothingRadius - dst;
		return v * v * v * SpikyPow3Normalization;
	}
	return 0;
}

float Simulation::DerivativeSpikyPow3(float dst)
{
	if (dst <= smoothingRadius)
	{
		float v = smoothingRadius - dst;
		return -v * v * SpikyPow3DerivativeNormalization;
	}
	return 0;
}

float Simulation::ViscosityKernel(float dst){
    float x = std::max(0.f, smoothingRadius2 - dst * dst);
    return x*x*x / viscosityKernelNormalization;
}

float Simulation::Poly6Lookup(float dstSquared){
    int index = int(dstSquared * invSmoothingRadius2 * lookupSize);
    return poly6Lookup[index];
}

float Simulation::SpikyDerivativeLookup(float dst){
    int index = int(dst * invSmoothingRadius * lookupSize);
    return spikyDerivativeLookup[index];
}

float Simulation::ViscosityKernelLookup(float dst){
    int index = int(dst * invSmoothingRadius * lookupSize);
    return viscosityKernelLookup[index];
}

float Simulation::SpikyKernelPow3Lookup(float dstSquared){
    int index = int(dstSquared * invSmoothingRadius2 * lookupSize);
    return spikyKernelPow3Lookup[index];
}

float Simulation::DerivativeSpikyPow3Lookup(float dst){
    int index = int(dst * invSmoothingRadius * lookupSize);
    return derivativeSpikyPow3Lookup[index];
}

float Simulation::GetParticleInfluence(int p1, int p2){
    float distanceSquared = sf::Vector2f(particles[p1].position - particles[p2].position).lengthSquared();
    return Simulation::Poly6Lookup(distanceSquared);
}

float Simulation::DensityToPressure(float density){
    float x = density / targetDensity;
    return std::max(0.f, (x - 1.f) * pressureMultiplier);
}

float Simulation::NearDensityToNearPressure(float nearDensity){
    return nearDensity * nearPressureMultiplier;
}

void Simulation::CalculateDensities(int start, int end){
    for (int i = start; i < end; i++){
        Particle* particle = &particles[i];
        if (particle->isStatic) continue;
        sf::Vector2i cellPos = Simulation::PosToCellPos(particle->position);
        float totalDensity = 0;
        float totalNearDensity = 0;
        for (int dy = -1; dy <= 1; dy++){
            for (int dx = -1; dx <= 1; dx++){
                int neighborCellPos = (cellPos.y + dy) * horizontalCells + (cellPos.x + dx);
                if (neighborCellPos < 0 || neighborCellPos >= grid.size()) continue;
                const std::vector<int>& indexes = grid[neighborCellPos];

                for (int otherParticleIndex : indexes){
                    Particle* otherParticle = &particles[otherParticleIndex];
                    if (otherParticle->isStatic) continue;

                    sf::Vector2f delta = otherParticle->predictedPosition - particle->predictedPosition;
                    float distanceSquared = abs(delta.dot(delta));
                    if (distanceSquared > smoothingRadius2) continue;

                    float densityInfluence = Simulation::Poly6Lookup(distanceSquared);
                    float nearDensityInfluence = Simulation::SpikyKernelPow3Lookup(distanceSquared);
                    totalDensity += particleMass * densityInfluence;
                    totalNearDensity += particleMass * nearDensityInfluence;
                }
            }
        }
        particle->density = totalDensity;
        particle->pressure = Simulation::DensityToPressure(totalDensity);
        particle->nearPressure = Simulation::NearDensityToNearPressure(totalNearDensity);
    }
}

void Simulation::ParallelCalculateDensities(int threadCount){
    std::vector<std::future<void>> futures;
    futures.reserve(threadCount);

    int step = particlesCount / threadCount;
    for (int i = 0; i < threadCount; i++){
        int start = step * i;
        int end = i == threadCount - 1 ? particlesCount : step * (i+1);

        futures.emplace_back(threadPool.push([this, start, end](int threadId) { Simulation::CalculateDensities(start, end); }));
    }

    for (auto& f : futures) f.get();
}

float Simulation::CalculateSharedPressure(float pressure1, float pressure2){
    return (pressure1 + pressure2) * 0.5f;
}

sf::Vector2f Simulation::CalculatePressureViscosityForce(Particle* particle){
    sf::Vector2f pos = particle->predictedPosition;
    sf::Vector2f vel = particle->velocity;

    sf::Vector2i cellPos = Simulation::PosToCellPos(pos);
    std::vector<int> neighbors = Simulation::GetNeighborCells(cellPos);

    sf::Vector2f totalForce;
    for (int dy = -1; dy <= 1; dy++){
        for (int dx = -1; dx <= 1; dx++){
            int neighborCellPos = (cellPos.y + dy) * horizontalCells + (cellPos.x + dx);
            if (neighborCellPos < 0 || neighborCellPos >= grid.size()) continue;
            const std::vector<int>& indexes = grid[neighborCellPos];

            for (int i = 0; i < indexes.size(); i++){
                if (indexes[i] == particle->index) continue;
                Particle* otherParticle = &particles[indexes[i]];
                if (otherParticle->isStatic) continue;
                
                sf::Vector2f delta = otherParticle->predictedPosition - pos;
                float distanceSquared = abs(delta.dot(delta));
                if (distanceSquared > smoothingRadius2) continue;

                float distance = std::sqrt(distanceSquared);

                sf::Vector2f direction = distanceSquared > 0 ? delta / distance : sf::Vector2f(0, 1);

                float slope = Simulation::SpikyDerivativeLookup(distance);
                float nearSlope = Simulation::DerivativeSpikyPow3Lookup(distance);

                float sharedPressure = Simulation::CalculateSharedPressure(particle->pressure, otherParticle->pressure);
                float sharedNearPressure = Simulation::CalculateSharedPressure(particle->nearPressure, otherParticle->nearPressure);
                
                sf::Vector2f force = -direction * sharedPressure * slope * particleMass / particle->density;
                force += -direction * sharedNearPressure * nearSlope * particleMass / particle->density;
                if (viscosity != 0.f){
                    float viscosityInfluence = Simulation::ViscosityKernelLookup(distance);
                    force += (otherParticle->velocity - vel) * viscosityInfluence * viscosity;
                }
                totalForce += force;
                //otherParticle->acceleration += -force / particleMass;
            }
        }
    }
    return totalForce;
}

float clamp01(float v) { 
    if (v < 0) return 0;
    if (v > 1) return 1;
    return v;
}

sf::Vector2f Simulation::GetExternalForces(int particleIndex){
    bool leftMouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    bool rightMouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
    const float radius = 100;
    float strength = 125;
    if (leftMouseDown && !rightMouseDown) strength *= 1;
    else if (rightMouseDown && !leftMouseDown) strength *= -1;
    else strength = 0;
    sf::Vector2f force = sf::Vector2f(0,-gravity * particleMass);
    if (!enableGravity) force = sf::Vector2f(0,0);

    if (strength != 0 && mode == SIMULATION && enableMouse){
        sf::Vector2i mousePos = sf::Mouse::getPosition();
        sf::Vector2f offset = sf::Vector2f(mousePos) - particles[particleIndex].position;
        float sqrDist = offset.lengthSquared();

        if (sqrDist < radius*radius){
            float dst = std::sqrt(sqrDist);
            float centreT = 1 - dst / radius;
            float gravityWeight = 1 - (centreT * clamp01(strength / 250));
            sf::Vector2f dirToCenter = offset / dst;
            sf::Vector2f acceleration = force * gravityWeight + dirToCenter * centreT * strength;
            force = particleMass * acceleration;
        }
    }
    return force;
}

sf::Vector2f Simulation::CalculateParticleForces(Particle* particle){
    sf::Vector2f force;
    force += Simulation::CalculatePressureViscosityForce(particle);
    force += Simulation::GetExternalForces(particle->index);
    return force;
}

void Simulation::CalculateForces(int start, int end){
    for (int i = start; i < end; i++){
        Particle* particle = &particles[i];
        if (particle->isStatic) continue;
        sf::Vector2f force = Simulation::CalculateParticleForces(particle);
        particle->acceleration = -force / particle->density;
    }
}

void Simulation::ParallelCalculatePressureViscosityForces(int threadCount){
    std::vector<std::future<void>> futures;
    futures.reserve(threadCount);

    int step = particlesCount / threadCount;
    for (int i = 0; i < threadCount; i++){
        int start = step * i;
        int end = i == threadCount - 1 ? particlesCount : step * (i+1);

        futures.emplace_back(threadPool.push([this, start, end](int threadId) { Simulation::CalculateForces(start, end); }));
    }
    for (auto& f : futures) f.get();
}

void Simulation::ApplyMovements(){
    for (int i = 0; i < particlesCount; i++){
        if (particles[i].isStatic) continue;
        particles[i].position += particles[i].velocity * deltaTime + particles[i].acceleration * 0.5f * deltaTime * deltaTime;
        particles[i].velocity += particles[i].acceleration * deltaTime;
    }
}

void Simulation::PredictPositions(){
    for (int i = 0; i < particlesCount; i++){
        if (particles[i].isStatic) continue;
        particles[i].predictedPosition = particles[i].position + particles[i].velocity * FIXED_DT;
    }
}

void Simulation::ResolveCollisions(){
    const float damping = 0.1f;
    const float circleRadiusSquared = circleRadius*circleRadius;
    for (int i = 0; i < particlesCount; i++){
        if (particles[i].isStatic) continue;
        sf::Vector2f& pos = particles[i].position;
        sf::Vector2i cellPos = Simulation::PosToCellPos(pos);
        if (pos.x < 0 || pos.x > 1920){
            if ((pos.x > 1920 && mode == AERODYNAMICS)) particles[i].isStatic = true;
            else {
                pos.x = std::clamp(pos.x, 0.f, 1920.f);
                particles[i].velocity.x *= -damping; 
                particles[i].velocity *= -damping;
            }
        }
        
        if ((pos.y < 0 && mode != AERODYNAMICS) || pos.y > 1080){
            pos.y = std::clamp(pos.y, 0.f, 1080.f);
            particles[i].velocity.y *= -damping;
            particles[i].velocity *= -damping;
        }

        if (circles.size() > 0){
            for (int dy = -1; dy <= 1; dy++){
                for (int dx = -1; dx <= 1; dx++){
                    int neighborCellPos = (cellPos.y + dy) * horizontalCells + (cellPos.x + dx);
                    if (neighborCellPos < 0 || neighborCellPos >= circleGrid.size()) continue;
                    std::vector<int>& indexes = circleGrid[neighborCellPos];

                    for (int c : indexes){
                        sf::Vector2f& circle = circles[c];
                        sf::Vector2f delta = circle - pos;
                        float dstSquared = delta.lengthSquared();
                        if (dstSquared < circleRadiusSquared){
                            float dst = std::sqrt(dstSquared);
                            sf::Vector2f normal = delta / dst;

                            pos = circle - normal * circleRadius;   // position correction
                            particles[i].velocity -= 2.f * particles[i].velocity.dot(normal) * normal;
                            particles[i].velocity *= -damping;
                        }
                    }
                }
            }
        }

        particles[i].position = pos;
    }
}

void Simulation::HandleInputs(){
    if (!enableMouse) return;
    sf::Vector2f mousePos = sf::Vector2f(sf::Mouse::getPosition());
    if (mode != SIMULATION && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)){
        mousePos.x = std::clamp(mousePos.x, 1.f, 1919.f);
        mousePos.y = std::clamp(mousePos.y, 1.f, 1079.f);
        circles.push_back(mousePos);

        sf::Vector2i cellPos = Simulation::PosToCellPos(mousePos);
        circleGrid[cellPos.y * horizontalCells + cellPos.x].push_back(circles.size()-1);
    } else if (mode != SIMULATION && sf::Mouse::isButtonPressed(sf::Mouse::Button::Right)){
        for (int i = 0; i < circles.size(); i++) {
            sf::Vector2f& circle = circles.at(i);
            if ((circle - mousePos).lengthSquared() > circleRadius*circleRadius*10) continue;
            circles.erase(circles.begin()+i);
        }
        circleGrid.clear();
        circleGrid.resize(horizontalCells * verticalCells);
        for (int i = 0; i < circles.size(); i++) { // Reorganize the grid (this is so inefficient)
            sf::Vector2f& circle = circles.at(i);
            sf::Vector2i cellPos = Simulation::PosToCellPos(circle);
            circleGrid[cellPos.y * horizontalCells + cellPos.x].push_back(i);
        }
    }

    if (paused && mode == SIMULATION && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
        if (!mousePressed){
            mousePressed = true;
            originalMousePos = mousePos;
            originalPosition = startingPosition;
        }
        startingPosition = originalPosition + mousePos - originalMousePos;
        Simulation::CreateParticles();
    }
}

void Simulation::Update(){
    if (paused) return;
    const int threadCount = threadPool.size();

    if (mode == PLAYGROUND && spawnParticles && particlesCount+1 < MAX_PARTICLES && timeElapsedSinceLastSpawn > 1.f / spawnsPerSecond){
        while (timeElapsedSinceLastSpawn > 1.f / spawnsPerSecond){
            sf::Vector2f randomPos(1920 / 2 - spawnRadius / 2 + rand() % (int)spawnRadius, 0);
            sf::Vector2f initialVelocity(0, 100);
            Particle p(randomPos, particlesCount);
            p.velocity = initialVelocity;
            Simulation::SpawnParticle(p);
            timeElapsedSinceLastSpawn -= 1.f / spawnsPerSecond;
        }
        timeElapsedSinceLastSpawn += deltaTime;
    } else if (mode == AERODYNAMICS){
        const float spawnRate = 1.f / 60.f;
        while (timeElapsedSinceLastSpawn > spawnRate){
            for (int i = 0; i < spawnsPerSecond; i++){
                sf::Vector2f pos(1, i / spawnsPerSecond * 1080);
                sf::Vector2f initialVelocity(100*gravity, 0);
                Particle p(pos, particlesCount);
                p.velocity = initialVelocity;
                Simulation::SpawnParticle(p);
            }
            timeElapsedSinceLastSpawn -= spawnRate;
        }
        timeElapsedSinceLastSpawn += deltaTime;
    }

    clock2.restart();
    deltaTime /= simulationSteps; // Deltatime is set by the main function with Simulation::SetDeltatime()
    deltaTime *= timeMultiplier;
    for (int i = 0; i < simulationSteps; i++){

        Simulation::PredictPositions();
        Simulation::CalculateParticleLookups();
        Simulation::ParallelCalculateDensities(threadCount);
        debugText.at(0).setString("Densities Calculation: " + std::to_string(clock2.restart().asMilliseconds()*simulationSteps));

        Simulation::ParallelCalculatePressureViscosityForces(threadCount);
        debugText.at(1).setString("Pressure Forces Calculation: " + std::to_string(clock2.restart().asMilliseconds()*simulationSteps));

        Simulation::ResolveCollisions();
        Simulation::ApplyMovements();
    }
}

float lerp(float a, float b, float t){
    return a + t * (b - a);
}

sf::Color lerp(sf::Color a, sf::Color b, float t){
    return sf::Color(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t));
}

bool contains(std::vector<int> v, int what){
    for (int item : v) if (item == what) return true;
    return false;
}

sf::Color Simulation::GetParticleColor(int i){
    if (particles[i].index != i) std::cout << "Error in particle index: " << particles[i].index << std::endl;
    if (contains(whiteParticles, i)) return sf::Color::White;
    float speed = abs(particles[i].velocity.length());
    float maxSpeed = 750.f;
    float gradient = std::min(speed / maxSpeed, 1.f);

    if (gradient < 0.5) return lerp(sf::Color::Blue, sf::Color::Cyan, gradient*2);
    else if (gradient < 0.7) return lerp(sf::Color::Cyan, sf::Color::Yellow, (gradient - 0.5f)*5);
    else return lerp(sf::Color::Yellow, sf::Color::Red, (gradient-0.7f)*(1.f/0.3f));
    return sf::Color::Magenta;
}

void Simulation::Draw(sf::RenderTexture& target){
    renderClock.restart();

    if (!particleVertexes.create(particlesCount*6)) std::cerr << "Couldn't create particle vertex buffer\n";
    const int circleSegments = 8;
    if (!circleVertexes.create(circles.size()*circleSegments*3)) std::cerr << "Couldn't create circle vertex buffer\n";
    sf::Vector2i mouseCell = Simulation::PosToCellPos(sf::Vector2f(sf::Mouse::getPosition()));
    sf::Vertex *vertexes = new sf::Vertex[particlesCount*6];
    
    for (int i = 0; i < particlesCount; ++i) {
        const sf::Vector2f& pos = particles[i].position;

        sf::Vertex* quad = &vertexes[i*6];
        sf::Color color = Simulation::GetParticleColor(i);

        sf::Vector2f tl = pos + sf::Vector2f(-particleRadius, -particleRadius);
        sf::Vector2f tr = pos + sf::Vector2f(particleRadius, -particleRadius);
        sf::Vector2f br = pos + sf::Vector2f(particleRadius, particleRadius);
        sf::Vector2f bl = pos + sf::Vector2f(-particleRadius, particleRadius);

        quad[0].position = tl; quad[0].color = color;
        quad[1].position = tr; quad[1].color = color;
        quad[2].position = br; quad[2].color = color;

        quad[3].position = br; quad[3].color = color;
        quad[4].position = bl; quad[4].color = color;
        quad[5].position = tl; quad[5].color = color;
    }
    if (!particleVertexes.create(particlesCount*6)) std::cerr << "Couldn't create vertex buffer\n";
    if (!particleVertexes.update(vertexes)) std::cerr << "Couldn't update vertex buffer\n";
    delete[] vertexes;

    const sf::Color circleColor = sf::Color::White;
    sf::Vertex *vertexesCircle = new sf::Vertex[circles.size()*circleSegments*3];
    float step = 2.f * PI / circleSegments;
    for (int i = 0; i < circles.size(); i++){
        sf::Vertex* circle = &vertexesCircle[i*circleSegments*3];
        sf::Vector2f center = circles[i];
        for (int x = 0; x < circleSegments; x++){
            sf::Vector2f pos1 = center + sf::Vector2f(std::sin(x * step), std::cos(x * step)) * circleRadius;
            sf::Vector2f pos2 = center + sf::Vector2f(std::sin((x + 1) * step), std::cos((x + 1) * step)) * circleRadius;

            int v = x * 3;
            circle[v].position = center; circle[v].color = circleColor;
            circle[v+1].position = pos1; circle[v+1].color = circleColor;
            circle[v+2].position = pos2; circle[v+2].color = circleColor;
        }
    }
    if (!circleVertexes.create(circles.size() * circleSegments * 3)) std::cerr << "Couldn't create circle vertex buffer\n";
    if (!circleVertexes.update(vertexesCircle)) std::cerr << "Couldn't update circle vertex buffer\n";
    delete[] vertexesCircle;

    target.draw(particleVertexes);
    target.draw(circleVertexes);

    const int characterSize = 16;
    for (int i = 0; i < 2; i++){
        target.draw(debugText.at(i));
    }

    if (mode == PLAYGROUND){
        sf::RectangleShape spawnRectangle(sf::Vector2f(spawnRadius, 5));
        spawnRectangle.setPosition(sf::Vector2f(1920 / 2 - spawnRadius / 2, 0));
        target.draw(spawnRectangle);
    }

    debugText.at(2).setString("Rendering: " + std::to_string(lastDelay));
    debugText.at(2).setPosition(sf::Vector2f(10, 2*characterSize*2+50));
    target.draw(debugText.at(2));

    if (particlesCount+1 >= MAX_PARTICLES){
        sf::Text text(font, "Max particles reached! (" + std::to_string(MAX_PARTICLES) + ")");
        text.setPosition(sf::Vector2f(1920/2 - text.getLocalBounds().size.x/2, 10));
        text.setFillColor(sf::Color::Red);
        target.draw(text);
    }

    lastDelay = renderClock.restart().asMilliseconds();
}

// Shapes
std::vector<sf::Vector2f> Simulation::Circle::GetPositions() const {
    std::vector<sf::Vector2f> list;
    list.push_back(sf::Vector2f(0,0));
    int counter = 8;
    int level = 1;
    for (int i = 0; i < density; i++){
        counter--;
        int total = level * 8;
        int d = 2 * PI * (counter/total);
        list.push_back(sf::Vector2f(sin(counter) * level, cos(counter) * level));
        if (counter == -1){
            level++;
            counter = level * 8;
        }
    }
    return list;
}