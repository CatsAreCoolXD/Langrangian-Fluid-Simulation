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

Simulation::Simulation(int particlesCount, float particleMass, float smoothingRadius, float targetDensity, float pressureMultiplier, float gravity, float viscosity, int simulationSteps) : particlesCount(particlesCount), initialParticlesCount(particlesCount),
        particleMass(particleMass), smoothingRadius(smoothingRadius), targetDensity(targetDensity), pressureMultiplier(pressureMultiplier), gravity(gravity), viscosity(viscosity), simulationSteps(simulationSteps), threadPool(std::thread::hardware_concurrency())
{
    std::cout << "Threads: " << threadPool.size() << std::endl;
    particles = new Particle[particlesCount];
    if (true){ // Enable if you want the particles to spawn in a square
        const float spawnSquareSize = 1024;
        const Vec2 topLeft = Vec2(1920 / 2 - spawnSquareSize / 2, 1080 / 2 - spawnSquareSize / 2);
        int grid = std::ceil(std::sqrt(particlesCount));
        const float stepSize = spawnSquareSize / grid;
        int i = 0;
        for (int x = 0; x < grid && i < particlesCount; x++){
            for (int y = 0; y < grid && i < particlesCount; y++){
                Vec2 pos = topLeft + Vec2(y * stepSize, x * stepSize);
                Particle p(pos, i);
                particles[i] = p;
                i++;
            }
        }
    }
    else {
        srand(0);
        for (int i = 0; i < particlesCount; i++){
            Vec2 randomPos(rand() % 1920, rand() % 1080);
            Particle p(randomPos, i);
            particles[i] = p;
        }
    }
    std::cout << "Spawned " << particlesCount << " particles\n";

    invSmoothingRadius = 1.f / smoothingRadius;
    smoothingRadius2 = smoothingRadius * smoothingRadius;
    invSmoothingRadius2 = 1.f / smoothingRadius2;

    cells = std::ceil(1920 / smoothingRadius) * std::ceil(1080 / smoothingRadius);
    smoothingKernelNormalization = 40.f / (7 * 3.14159f * smoothingRadius * smoothingRadius); // 2 dimensions
    poly6Normalization = (4.f / (3.14159f * pow(smoothingRadius, 8)));
    spikyNormalization = (10.f / (3.14159f * pow(smoothingRadius, 5)));
    spikyDerivativeNormalization = -30.f / (3.14159f * pow(smoothingRadius, 5));
    viscosityKernelNormalization = (3.14159f * pow(smoothingRadius, 8)) / 4;

    std::cout << "Precomputed kernel normalizations\n";

    poly6Lookup.resize(lookupSize+1, 0.f);
    spikyDerivativeLookup.resize(lookupSize+1, 0.f);
    viscosityKernelLookup.resize(lookupSize+1, 0.f);
    float lookupSizef = lookupSize;
    for (int i = 0; i < lookupSize+1; i++){
        float iF = i;
        float q2 = iF / lookupSize;
        float r = std::sqrt(q2) * smoothingRadius;
        poly6Lookup.at(i) = Simulation::Poly6(r);
        spikyDerivativeLookup.at(i) = Simulation::SpikyDerivative(iF / lookupSizef * smoothingRadius);
        viscosityKernelLookup.at(i) = Simulation::ViscosityKernel(iF / lookupSizef * smoothingRadius);
    }
    std::cout << "Precomputed kernel lookup tables\n";

    particleVertexes = sf::VertexBuffer(sf::PrimitiveType::Triangles, sf::VertexBuffer::Usage::Stream);
    circleVertexes = sf::VertexBuffer(sf::PrimitiveType::Triangles, sf::VertexBuffer::Usage::Dynamic);
    std::cout << "Loaded Vertex Buffer\n";

    if (!font.openFromFile("Fonts/OpenSans-Bold.ttf")) std::cerr << "Couldn't load font file\n";
    int characterSize = 16;
    debugText.resize(3, sf::Text(font, "default text", characterSize));
    for (int i = 0; i < 3; i++) {
        sf::Text newText(font, "default text", characterSize);
        newText.setFillColor(sf::Color::White);
        newText.setPosition(Vec2(10, std::round(i*characterSize*2+50)));
        debugText.at(i) = newText;
    }

    horizontalCells = std::ceil(1920.f / smoothingRadius);
    verticalCells = std::ceil(1080.f / smoothingRadius);
    circleGrid.clear();
    circleGrid.resize(horizontalCells * verticalCells);
}

void Simulation::Reset(){
    circleGrid.clear();
    circleGrid.resize(horizontalCells * verticalCells);
    circles.clear();
    if (mode == SIMULATION){ // Enable if you want the particles to spawn in a square
        particlesCount = initialParticlesCount;
        delete[] particles;
        particles = new Particle[particlesCount];
        const float spawnSquareSize = 1024;
        const Vec2 topLeft = Vec2(1920 / 2 - spawnSquareSize / 2, 1080 / 2 - spawnSquareSize / 2);
        int grid = std::ceil(std::sqrt(particlesCount));
        const float stepSize = spawnSquareSize / grid;
        int i = 0;
        for (int x = 0; x < grid && i < particlesCount; x++){
            for (int y = 0; y < grid && i < particlesCount; y++){
                Vec2 pos = topLeft + Vec2(y * stepSize, x * stepSize);
                Particle p(pos, i);
                particles[i] = p;
                i++;
            }
        }
    } else if (mode == PLAYGROUND){
        particlesCount = 0;
        delete[] particles;
        particles = new Particle[MAX_PARTICLES];
    }
}

void Simulation::SpawnParticle(Particle& particle){
    if (particlesCount+1 == MAX_PARTICLES) {
        std::cerr << "Error: Max particles reached: " << MAX_PARTICLES << std::endl;
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

sf::Vector2i Simulation::PosToCellPos(Vec2 pos){
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

// float Simulation::Spiky(float dst){
//     float x = smoothingRadius - dst;
//     return spikyNormalization * x*x*x;
// }

float Simulation::SpikyDerivative(float dst){
    float x = smoothingRadius - dst;
    return spikyDerivativeNormalization * x * x;

    #ifdef OTHER_SPIKY_DERIVATIVE
    const float SpikyPow2DerivativeScalingFactor = 12.f / (pow(smoothingRadius, 4) * 3.14159f);
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

float Simulation::GetParticleInfluence(int p1, int p2){
    float distanceSquared = Vec2(particles[p1].position - particles[p2].position).lengthSquared();
    return useKernelLookups ? Simulation::Poly6Lookup(distanceSquared) : Simulation::Poly6(std::sqrt(distanceSquared));
}

float Simulation::DensityToPressure(float density){
    float x = density / targetDensity;
    return std::max(0.f, (x - 1.f) * pressureMultiplier);
}

void Simulation::ParallelCalculateDensities(int start, int end){
    for (int i = start; i < end; i++){
        Particle* particle = &particles[i];
        sf::Vector2i cellPos = Simulation::PosToCellPos(particle->position);
        float totalDensity = 0;
        for (int dy = -1; dy <= 1; dy++){
            for (int dx = -1; dx <= 1; dx++){
                int neighborCellPos = (cellPos.y + dy) * horizontalCells + (cellPos.x + dx);
                if (neighborCellPos < 0 || neighborCellPos >= grid.size()) continue;
                const std::vector<int>& indexes = grid[neighborCellPos];

                for (int otherParticleIndex : indexes){
                    Particle* otherParticle = &particles[otherParticleIndex];

                    Vec2 delta = otherParticle->predictedPosition - particle->predictedPosition;
                    float distanceSquared = abs(delta.dot(delta));
                    if (distanceSquared > smoothingRadius2) continue;

                    float influence = useKernelLookups ? Simulation::Poly6Lookup(distanceSquared) : Simulation::Poly6(std::sqrt(distanceSquared));
                    totalDensity += particleMass * influence;
                }
            }
        }
        particle->density = totalDensity;
        particle->pressure = Simulation::DensityToPressure(totalDensity);
    }
}

void Simulation::CalculateDensities(int threadCount){
    std::vector<std::future<void>> futures;
    futures.reserve(threadCount);

    int step = particlesCount / threadCount;
    for (int i = 0; i < threadCount; i++){
        int start = step * i;
        int end = i == threadCount - 1 ? particlesCount : step * (i+1);

        futures.emplace_back(threadPool.push([this, start, end](int threadId) { Simulation::ParallelCalculateDensities(start, end); }));
    }

    for (auto& f : futures) f.get();
}

float Simulation::DensityAtPoint(Vec2 pos){
    sf::Vector2i cellPos = Simulation::PosToCellPos(pos);
    std::vector<int> neighbors = Simulation::GetNeighborCells(cellPos);

    float totalDensity = 0;
    for (int dy = -1; dy <= 1; dy++){
        for (int dx = -1; dx <= 1; dx++){
            int neighborCellPos = (cellPos.y + dy) * horizontalCells + (cellPos.x + dx);
            if (neighborCellPos < 0 || neighborCellPos >= grid.size()) continue;
            const std::vector<int>& indexes = grid[neighborCellPos];

            for (int i = 0; i < indexes.size(); i++){
                Particle* otherParticle = &particles[indexes[i]];

                Vec2 delta = otherParticle->predictedPosition - pos;
                float distanceSquared = abs(delta.dot(delta));
                if (distanceSquared > smoothingRadius2) continue;

                float influence = useKernelLookups ? Simulation::Poly6Lookup(distanceSquared) : Simulation::Poly6(std::sqrt(distanceSquared));
                totalDensity += particleMass * influence;
            }
        }
    }
    return totalDensity;
}

float Simulation::CalculateSharedPressure(float pressure1, float pressure2){
    return (pressure1 + pressure2) * 0.5f;
}

Vec2 Simulation::CalculatePressureViscosityForce(Particle* particle){
    Vec2 pos = particle->predictedPosition;
    Vec2 vel = particle->velocity;;
    const float density = particle->density;
    const float pressure = particle->pressure;
    const float invDensity = 1.f / density;
    const float radius2 = smoothingRadius2;

    sf::Vector2i cellPos = Simulation::PosToCellPos(pos);
    std::vector<int> neighbors = Simulation::GetNeighborCells(cellPos);

    Vec2 totalForce;
    for (int dy = -1; dy <= 1; dy++){
        for (int dx = -1; dx <= 1; dx++){
            int neighborCellPos = (cellPos.y + dy) * horizontalCells + (cellPos.x + dx);
            if (neighborCellPos < 0 || neighborCellPos >= grid.size()) continue;
            const std::vector<int>& indexes = grid[neighborCellPos];

            for (int i = 0; i < indexes.size(); i++){
                if (indexes[i] == particle->index) continue;
                Particle& otherParticle = particles[indexes[i]];
                
                Vec2 delta = otherParticle.predictedPosition - pos;
                float distanceSquared = abs(delta.dot(delta));
                if (distanceSquared > radius2) continue;
                float distance = std::sqrt(distanceSquared);

                Vec2 direction = distanceSquared > 0 ? delta / distance : Vec2(0, 1);

                float slope = useKernelLookups ? Simulation::SpikyDerivativeLookup(distance) : Simulation::SpikyDerivative(distance);

                float sharedPressure = Simulation::CalculateSharedPressure(pressure, otherParticle.pressure);
                Vec2 force = -direction * sharedPressure * slope * particleMass * invDensity;
                if (viscosity != 0.f){
                    float viscosityInfluence = useKernelLookups ? Simulation::ViscosityKernelLookup(distance) : Simulation::ViscosityKernel(distance);
                    force += (otherParticle.velocity - vel) * viscosityInfluence * viscosity;
                }
                totalForce += force;
            }
        }
    }
    return totalForce;
}

Vec2 Simulation::GetInteractionForce(int particleIndex){
    bool leftMouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    bool rightMouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
    const float radius = 75;
    float strength = 5000000;
    if (leftMouseDown && !rightMouseDown) strength *= 1;
    else if (rightMouseDown && !leftMouseDown) strength *= -1;
    else return Vec2(0,0);
    sf::Vector2i mousePos = sf::Mouse::getPosition();
    Vec2 force = Vec2(0,0);
    Vec2 offset = Vec2(mousePos) - particles[particleIndex].position;
    float sqrDist = offset.lengthSquared();

    if (sqrDist < radius * radius){
        float dst = std::sqrt(sqrDist);
        Vec2 dirToInputPoint = offset / dst;
        float centreT = 1 - dst / radius;
        force = (dirToInputPoint * strength - particles[particleIndex].velocity) * centreT;
    }
    return force / pressureMultiplier;
}

Vec2 Simulation::CalculateParticleForces(Particle* particle){
    Vec2 force;
    if (enableGravity) force += Vec2(0.f, -gravity * particleMass);
    force += Simulation::CalculatePressureViscosityForce(particle);
    if (mode == SIMULATION && enableMouse) force += Simulation::GetInteractionForce(particle->index);
    return force;
}

void Simulation::ParallelCalculateForces(int start, int end){
    for (int i = start; i < end; i++){
        Particle* particle = &particles[i];
        Vec2 force = Simulation::CalculateParticleForces(particle);
        particle->acceleration = -force / particle->density;
    }
}

void Simulation::CalculatePressureViscosityForces(int threadCount){
    std::vector<std::future<void>> futures;
    futures.reserve(threadCount);

    int step = particlesCount / threadCount;
    for (int i = 0; i < threadCount; i++){
        int start = step * i;
        int end = i == threadCount - 1 ? particlesCount : step * (i+1);

        futures.emplace_back(threadPool.push([this, start, end](int threadId) { Simulation::ParallelCalculateForces(start, end); }));
    }

    for (auto& f : futures) f.get();
}

//#define USE_RK4
#ifdef USE_RK4
struct State {
    Vec2 pos;
    Vec2 vel;
};

struct fState {
    Vec2 dPos;
    Vec2 dVel;
};

fState eval(const State& state, float gravity, float mass){
    fState out;
    out.dPos = state.vel;
    out.dVel = Vec2(0, -1) * mass * gravity;
    return out;
}
#endif

void Simulation::ApplyMovements(){
    for (int i = 0; i < particlesCount; i++){
        #ifndef USE_RK4
        particles[i].position += particles[i].velocity * deltaTime + particles[i].acceleration * 0.5f * deltaTime * deltaTime;
        #endif
        
        #ifdef USE_RK4

        State s1;
        s1.pos = particles[i].position;
        s1.vel = particles[i].velocity;

        fState k1 = eval(s1, gravity, particleMass);

        State s2;
        s2.pos = s1.pos + k1.dPos * (deltaTime * 0.5f);
        s2.vel = s1.vel + k1.dVel * (deltaTime * 0.5f);
        fState k2 = eval(s2, gravity, particleMass);

        State s3;
        s3.pos = s1.pos + k2.dPos * (deltaTime * 0.5f);
        s3.vel = s1.vel + k2.dVel * (deltaTime * 0.5f);
        fState k3 = eval(s3, gravity, particleMass);

        State s4;
        s4.pos = s1.pos + k3.dPos * deltaTime;
        s4.vel = s1.vel + k3.dVel * deltaTime;
        fState k4 = eval(s4, gravity, particleMass);

        particles[i].position += (k1.dPos + 2.f * k2.dPos + 2.f * k3.dPos + k4.dPos) * (deltaTime / 6.f);

        #endif

        particles[i].velocity += particles[i].acceleration * deltaTime;
    }
}

void Simulation::PredictPositions(){
    for (int i = 0; i < particlesCount; i++){
        particles[i].predictedPosition = particles[i].position + particles[i].velocity * FIXED_DT;
    }
}

void Simulation::ResolveCollisions(){
    const float damping = 0.1f;
    const float circleRadiusSquared = circleRadius*circleRadius;
    for (int i = 0; i < particlesCount; i++){
        Vec2& pos = particles[i].position;
        sf::Vector2i cellPos = Simulation::PosToCellPos(pos);
        if (pos.x < 0 || pos.x > 1920){
            pos.x = std::clamp(pos.x, 0.f, 1920.f);
            particles[i].velocity.x *= -damping;
            particles[i].velocity *= -damping;
        }
        
        if (pos.y < 0 || pos.y > 1080){
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
                        Vec2& circle = circles[c];
                        Vec2 delta = circle - pos;
                        float dstSquared = delta.lengthSquared();
                        if (dstSquared < circleRadiusSquared){
                            float dst = std::sqrt(dstSquared);
                            Vec2 normal = delta / dst;

                            pos = circle - normal * circleRadius;   // position correction
                            particles[i].velocity -=
                                2.f * particles[i].velocity.dot(normal) * normal;
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
    if (enableMouse && mode == PLAYGROUND && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)){
        sf::Vector2f mousePos = sf::Vector2f(sf::Mouse::getPosition());
        circles.push_back(mousePos);

        sf::Vector2i cellPos = Simulation::PosToCellPos(mousePos);
        circleGrid[cellPos.y * horizontalCells + cellPos.x].push_back(circles.size()-1);
    }
}

void Simulation::Update(){
    const int threadCount = threadPool.size();

    timeElapsedSinceLastSpawn += deltaTime;
    if (mode == PLAYGROUND && spawnParticles && particlesCount+1 < MAX_PARTICLES && timeElapsedSinceLastSpawn > 1.f / spawnsPerSecond){
        Vec2 randomPos(1920 / 2 - spawnRadius / 2 + rand() % (int)spawnRadius, 0);
        Vec2 initialVelocity(0, 100);
        Particle p(randomPos, particlesCount);
        p.velocity = initialVelocity;
        Simulation::SpawnParticle(p);
        timeElapsedSinceLastSpawn -= 1.f / spawnsPerSecond;
    }

    clock2.restart();
    deltaTime /= simulationSteps;
    deltaTime *= timeMultiplier;
    for (int i = 0; i < simulationSteps; i++){

        Simulation::PredictPositions();
        Simulation::CalculateParticleLookups();
        Simulation::CalculateDensities(threadCount);
        debugText.at(0).setString("Densities Calculation: " + std::to_string(clock2.restart().asMilliseconds()));

        Simulation::CalculatePressureViscosityForces(threadCount);
        debugText.at(1).setString("Pressure Forces Calculation: " + std::to_string(clock2.restart().asMilliseconds()));

        Simulation::ApplyMovements();
        Simulation::ResolveCollisions();
    }
}

float lerp(float a, float b, float t){
    return a + t * (b - a);
}

sf::Color lerp(sf::Color a, sf::Color b, float t){
    return sf::Color(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t));
}

sf::Color Simulation::GetParticleColor(int i){
    float speed = particles[i].velocity.length();
    float maxSpeed = 1000.f;
    float gradient = std::min(speed / maxSpeed, 1.f);

    if (gradient < 0.5) return lerp(sf::Color::Blue, sf::Color::Cyan, gradient*2);
    else if (gradient < 0.7) return lerp(sf::Color::Cyan, sf::Color::Yellow, (gradient - 0.5f)*5);
    else return lerp(sf::Color::Yellow, sf::Color::Red, (gradient-0.7f)*(1.f/0.3f));
    return sf::Color::Magenta;
}

void Simulation::Draw(sf::RenderTexture& target){
    renderClock.restart();

    const float radius = 6;
    if (!particleVertexes.create(particlesCount*6)) std::cerr << "Couldn't create particle vertex buffer\n";
    const int circleSegments = 8;
    if (!circleVertexes.create(circles.size()*circleSegments*3)) std::cerr << "Couldn't create circle vertex buffer\n";
    sf::Vector2i mouseCell = Simulation::PosToCellPos(sf::Mouse::getPosition());
    sf::Vertex *vertexes = new sf::Vertex[particlesCount*6];
    
    for (int i = 0; i < particlesCount; ++i) { // Draw every particle as a quad
        const Vec2& pos = particles[i].position;

        sf::Vertex* quad = &vertexes[i*6];
        sf::Color color = Simulation::GetParticleColor(i);

        Vec2 tl = pos + Vec2(-radius, -radius);
        Vec2 tr = pos + Vec2(radius, -radius);
        Vec2 br = pos + Vec2(radius, radius);
        Vec2 bl = pos + Vec2(-radius, radius);

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
    float step = 2.f * 3.14159f / circleSegments;
    for (int i = 0; i < circles.size(); i++){ // Draw every circle
        sf::Vertex* circle = &vertexesCircle[i*circleSegments*3];
        Vec2 center = circles[i];
        for (int x = 0; x < circleSegments; x++){
            Vec2 pos1 = center + Vec2(std::sin(x * step), std::cos(x * step)) * circleRadius;
            Vec2 pos2 = center + Vec2(std::sin((x + 1) * step), std::cos((x + 1) * step)) * circleRadius;

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

    debugText.at(2).setString("Rendering: " + std::to_string(lastDelay).substr(0));
    debugText.at(2).setPosition(Vec2(10, 2*characterSize*2+50));
    target.draw(debugText.at(2));

    lastDelay = renderClock.restart().asMilliseconds();
}