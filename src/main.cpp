#include <SFML/Graphics.hpp>
#include "simulation.h"
#include "gui.h"
#include <chrono>
#include <iostream>
#include <cmath>

// cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
//cmake --build build

float GetFps(std::vector<float> list){
    float total = 0;
    for (int i = 0; i < list.size(); i++){
        total += list[i];
    }
    return total / list.size();
}

int main()
{
    auto window = sf::RenderWindow(sf::VideoMode::getDesktopMode(), "Fluid Simulation", sf::State::Fullscreen);
    window.setFramerateLimit(120);
    window.setView(window.getDefaultView());

    if (!sf::Shader::isAvailable()){
        std::cout << "Your system does not support shaders\n";
        return 0;
    }

    std::cout << "Initializing simulation\n";
    int particleCount = 8192;
    float particleMass = 1;
    float smoothingRadius = 25;
    float targetDensity = 0.007f;
    float pressureMultiplier = 10000.f;
    float gravity = 9.81f;
    float viscosity = 0.35f;
    int simulationSteps = 3;
    int mode = SIMULATION;
    Simulation sim(particleCount, particleMass, smoothingRadius, targetDensity, pressureMultiplier, gravity, viscosity, simulationSteps);
    sim.SetDeltatime(1.f / 120.f);
    sim.Update();
    std::cout << "Initialized simulation\n";

    bool paused = true, settingsWindow = false, spawnParticles = false;

    sf::RenderTexture render(sf::Vector2u(1920u, 1080u));

    sf::Clock clock;

    sf::Font font("Fonts/OpenSans-Bold.ttf");
    sf::Text fpsText(font);

    float sliderRange = 1;
    float newTargetDensity = targetDensity;
    Slider targetDensitySlider(&newTargetDensity, 0.0001, 0.02f, sf::Vector2f(200, 5), sf::Vector2f(1600, 50), "Target Density");

    float newPressureMultiplier = pressureMultiplier;
    Slider pressureMultiplierSlider(&newPressureMultiplier, 1, 100000, sf::Vector2f(200, 5), sf::Vector2f(1600, 125), "Pressure Multiplier");
    
    float newViscosity = viscosity;
    Slider viscositySlider(&newViscosity, 0, 1, sf::Vector2f(200, 5), sf::Vector2f(1600, 200), "Viscosity");
    
    float newGravity = gravity;
    Slider gravitySlider(&newGravity, -20, 20, sf::Vector2f(200, 5), sf::Vector2f(1600, 275), "Gravity");

    bool useGravity = true;
    CheckBox gravityCheckBox(&useGravity, "Use Gravity", sf::Vector2f(1550, 350), sf::Vector2f(11,11));
    
    bool useKernelLookups = true;
    CheckBox kernelLookupsCheckBox(&useKernelLookups, "Use Kernel Lookups", sf::Vector2f(1550, 400), sf::Vector2f(11,11));

    sf::Text settingsWindowText(font, "Press H to toggle settings window", 16);
    settingsWindowText.setPosition(sf::Vector2f(1600,5));

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

        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            if (auto* keyEvent = event->getIf<sf::Event::KeyPressed>()){
                if (keyEvent->scancode == sf::Keyboard::Scancode::Space){
                    paused = !paused;
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::Escape){
                    window.close();
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::R){
                    sim.Reset();
                    paused = true;
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::H){
                    settingsWindow = !settingsWindow;
                } else if (keyEvent->scancode == sf::Keyboard::Scancode::Tab){
                    mode = (mode + 1) % 2;
                    sim.SetMode(mode);
                } else if (mode == PLAYGROUND && keyEvent->scancode == sf::Keyboard::Scancode::E){
                    spawnParticles = !spawnParticles;
                    sim.EnableParticleSpawner(spawnParticles);
                }
            }
        }

        sf::Vector2f mousePos(sf::Mouse::getPosition(window));

        sim.SetInteractionAbility(!settingsWindow);

        sim.SetDeltatime(deltaTime);
        if (!paused) sim.Update();
        sim.HandleInputs();
        sim.Draw(render);

        if (!settingsWindow) render.draw(settingsWindowText);
        if (settingsWindow){
            targetDensitySlider.Draw(render, mousePos);
            sim.SetTargetDensity(newTargetDensity);

            pressureMultiplierSlider.Draw(render, mousePos);
            sim.SetPressureMultiplier(newPressureMultiplier);
            
            viscositySlider.Draw(render, mousePos);
            sim.SetViscosity(newViscosity);

            gravitySlider.Draw(render, mousePos);
            gravityCheckBox.Draw(render, mousePos);
            sim.SetGravity(newGravity, useGravity);

            kernelLookupsCheckBox.Draw(render, mousePos);
            sim.SetKernelLookup(useKernelLookups);
        }
        

        float avgFps = GetFps(fpsList);
        std::string fpsString = (std::string)"FPS: " + std::to_string(avgFps);
        fpsText.setString(fpsString);
        render.draw(fpsText);

        // sf::CircleShape mouseCircle(smoothingRadius);
        // mouseCircle.setPosition(mousePos - sf::Vector2f(smoothingRadius, smoothingRadius));
        // sf::Color mouseCircleColor(100u, 200u, 100u, 100u);
        // mouseCircle.setFillColor(mouseCircleColor);
        // render.draw(mouseCircle);

        // float densityAtMouse = sim.DensityAtPoint(mousePos);
        // sf::Text densityText(font);
        // std::string dt = "Density: " + std::to_string(densityAtMouse);
        // densityText.setString(dt);
        // densityText.setPosition(mousePos);
        // render.draw(densityText);

        render.display();
        const sf::Texture& texture = render.getTexture();
        sf::Sprite sprite(texture);
        window.draw(sprite);

        window.display();
    }
}
