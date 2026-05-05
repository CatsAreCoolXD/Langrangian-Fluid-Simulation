#include <SFML/Graphics.hpp>
#include "simulation.h"
#include "gui.h"
#include <chrono>
#include <iostream>
#include <cmath>

// cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
// cmake --build build

float GetAverage(std::vector<float> list){ // Returns the average float in a list of floats
    float total = 0;
    for (int i = 0; i < list.size(); i++){
        total += list[i];
    }
    return total / list.size();
}

int main()
{
    // Window
    std::cout << "Initiliazing window" << std::endl;
    sf::ContextSettings settings;
    settings.antiAliasingLevel = 8;
    sf::RenderWindow window = sf::RenderWindow();
    window.create(sf::VideoMode({1919, 1080}), "Fluid Simulation", sf::Style::None, sf::State::Windowed, settings);
    std::cout << "Window created" << std::endl;
    window.setFramerateLimit(120);
    window.setView(window.getDefaultView());

    if (!sf::Shader::isAvailable()){
        std::cout << "Your system does not support shaders\n";
        return 0;
    }

    // Simulation
    std::cout << "Initializing simulation\n";
    int particleCount = 8192; // Best: 8192 particles.
    float particleMass = 1;
    float smoothingRadius = 25; // For 8k particles, 25 gives the best results.
    float targetDensity = 0.007f; // For 8k particles, 0.007 gives the best results.
    float pressureMultiplier = 10000.f;
    float nearPressureMultiplier = 10000.f;
    float gravity = 9.81f;
    float viscosity = 0.35f;
    int simulationSteps = 5;
    int mode = SIMULATION;
    Simulation sim(particleCount, particleMass, smoothingRadius, targetDensity, pressureMultiplier, nearPressureMultiplier, gravity, viscosity, simulationSteps);
    sim.SetDeltatime(1.f / 120.f);
    std::cout << "Initialized simulation\n";


    sf::RenderTexture render(sf::Vector2u(1920u, 1080u));

    sf::Clock clock;

    sf::Font font("Fonts/OpenSans-Bold.ttf");
    sf::Text fpsText(font);

    bool settingsWindow = false;
    // Settings
    float newTargetDensity = 1;
    Slider targetDensitySlider(&newTargetDensity, 0.f, 10.f, sf::Vector2f(200, 5), sf::Vector2f(1600, 50), "Target Density");

    float newPressureMultiplier = 1;
    Slider pressureMultiplierSlider(&newPressureMultiplier, 0, 10, sf::Vector2f(200, 5), sf::Vector2f(1600, 125), "Pressure Multiplier");
    
    float newNearPressureMultiplier = 1;
    Slider nearPressureMultiplierSlider(&newNearPressureMultiplier, 0, 10, sf::Vector2f(200, 5), sf::Vector2f(1600, 200), "Near Pressure Multiplier");
    
    Slider viscositySlider(&sim.GetViscosity(), 0, 1, sf::Vector2f(200, 5), sf::Vector2f(1600, 275), "Viscosity");
    
    float newGravity = gravity;
    Slider gravitySlider(&newGravity, -20, 20, sf::Vector2f(200, 5), sf::Vector2f(1600, 350), "Gravity");
    
    Slider timeSlider(&sim.GetTimeMultiplier(), 0.01f, 3, sf::Vector2f(200, 5), sf::Vector2f(1600, 425), "Time Multiplier");

    bool useGravity = true;
    CheckBox gravityCheckBox(&useGravity, "Use Gravity", sf::Vector2f(1550, 475), sf::Vector2f(11,11));

    float newParticlesCount = particleCount;
    Slider particlesSlider(&newParticlesCount, 1, 50000, sf::Vector2f(200, 5), sf::Vector2f(1600, 550), "Particles count");

    float newSmoothingRadius = smoothingRadius;
    Slider smoothingRadiusSlider(&newSmoothingRadius, 1, 50, sf::Vector2f(200, 5), sf::Vector2f(1600, 625), "Smoothing Radius");

    float newSimulationSteps = simulationSteps;
    Slider simulationStepsSlider(&newSimulationSteps, 1, 20, sf::Vector2f(200, 5), sf::Vector2f(1600, 700), "Simulation Steps");

    float spawnSquareSize = 1024;
    Slider spawnSlider(&spawnSquareSize, 1, 2048, sf::Vector2f(200, 5), sf::Vector2f(1600, 775), "Spawn Square Size");

    Slider particlesSizeSlider(&sim.GetParticlesRadius(), 0.1f, 10.f, sf::Vector2f(200, 5), sf::Vector2f(1600, 850), "Particles Draw Size");

    Slider spawnsPerSecondSlider(&sim.GetSpawnsPerSecond(), 1.f, 1000.f, sf::Vector2f(200, 5), sf::Vector2f(1600, 925), "Spawns Per Second (Playground Mode)");

    sf::Text settingsWindowText(font, "Press H to toggle settings window", 16);
    settingsWindowText.setPosition(sf::Vector2f(1600,5));

    SelectionMenu modeMenu(&mode, {"Simulation", "Playground", "Aerodynamics"}, sf::Vector2f(10, 175), sf::Vector2f(250, 50));

    std::vector<float> fpsList;

    // Main game loop
    while (window.isOpen())
    {
        float deltaTime = clock.restart().asSeconds();
        float fps = 1.f / deltaTime;
        fpsList.push_back(fps);
        if (fpsList.size() > 30) fpsList.erase(fpsList.begin());

        render.clear();
        window.clear();

        sf::Vector2f mousePos(sf::Mouse::getPosition(window));

        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            if (auto* keyEvent = event->getIf<sf::Event::KeyPressed>()){
                if (keyEvent->scancode == sf::Keyboard::Scancode::Space){
                    sim.Pause();
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::Escape){
                    window.close();
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::R){
                    sim.Reset();
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::H){
                    settingsWindow = !settingsWindow;
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::Tab){
                    mode = (mode + 1) % 2;
                    sim.SetMode(mode);
                    std::cout << "Changed mode to " << mode << std::endl;
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::E){
                    if (mode == PLAYGROUND){
                        sim.ToggleParticleSpawner();
                    } else if (mode == SIMULATION || mode == AERODYNAMICS){
                        Particle whiteParticle(mousePos, sim.GetParticleCount());
                        whiteParticle.velocity = sf::Vector2f(0,0);
                        sim.AddWhiteParticle();
                        sim.SpawnParticle(whiteParticle);
                    }
                }
            }
        }

        sim.SetInteractionAbility(!settingsWindow);

        sim.SetDeltatime(deltaTime);
        sim.Update();
        sim.HandleInputs();
        sim.Draw(render);

        if (!settingsWindow) render.draw(settingsWindowText);
        if (settingsWindow){
            targetDensitySlider.Draw(render, mousePos);
            sim.SetTargetDensity(newTargetDensity * targetDensity);

            pressureMultiplierSlider.Draw(render, mousePos);
            sim.SetPressureMultiplier(newPressureMultiplier * pressureMultiplier);

            nearPressureMultiplierSlider.Draw(render, mousePos);
            sim.SetNearPressureMultiplier(newNearPressureMultiplier * nearPressureMultiplier);
            
            viscositySlider.Draw(render, mousePos);

            gravitySlider.Draw(render, mousePos);
            gravityCheckBox.Draw(render, mousePos);
            sim.SetGravity(newGravity, useGravity);

            timeSlider.Draw(render, mousePos);

            particlesSlider.Draw(render, mousePos);
            if (mode == SIMULATION) sim.SetParticlesAmount(newParticlesCount);

            smoothingRadiusSlider.Draw(render, mousePos);
            sim.SetSmoothingRadius(newSmoothingRadius);

            simulationStepsSlider.Draw(render, mousePos);
            sim.SetSimulationSteps(newSimulationSteps);

            spawnSlider.Draw(render, mousePos);
            sim.SetStartingSize(spawnSquareSize);

            particlesSizeSlider.Draw(render, mousePos);

            spawnsPerSecondSlider.Draw(render, mousePos);

        }
        modeMenu.Draw(render, mousePos);
        sim.SetMode(mode);

        float avgFps = GetAverage(fpsList);
        std::string fpsString = (std::string)"FPS: " + std::to_string(avgFps);
        fpsText.setString(fpsString);
        render.draw(fpsText);

        render.display();
        const sf::Texture& texture = render.getTexture();
        sf::Sprite sprite(texture);
        window.draw(sprite);

        window.display();
    }
}
