#include "gui.h"
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <iostream>

Slider::Slider(float* value, float min, float max, sf::Vector2f size, sf::Vector2f pos, std::string valueName) : value(value), min(min), max(max), size(size), pos(pos), valueName(valueName) { 
    knobX = (*value - min) / (max-min) * size.x; 
    if (!font.openFromFile("Fonts/OpenSans-Bold.ttf")) std::cerr << "Couldn't load slider font for " << valueName << std::endl; 
    const float characterSize = 11;
    texts.push_back(sf::Text(font, std::to_string(min), characterSize));
    texts.push_back(sf::Text(font, std::to_string(max), characterSize));
    texts.push_back(sf::Text(font, std::to_string(*value), characterSize));
    texts.push_back(sf::Text(font, valueName, characterSize));

    texts.at(0).setPosition(pos - sf::Vector2f(50, 5));
    texts.at(1).setPosition(pos + sf::Vector2f(size.x+10, -5));
    texts.at(2).setPosition(pos - sf::Vector2f(50, 30));
}

void Slider::Draw(sf::RenderTexture& target, sf::Vector2f mousePos){
    bool mouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    if (!mouseDown) dragging = false;

    const float knobRadius = 15;
    if (!dragging && mousePos.x > pos.x + knobX - knobRadius && mousePos.x < pos.x + knobX + knobRadius){
        if (mousePos.y > pos.y - knobRadius && mousePos.y < pos.y + knobRadius) {
            hovering = true;
            if (mouseDown) {
                dragging = true;
                mouseOffset = mousePos - sf::Vector2f(pos.x + knobX, pos.y);
            }
        } else hovering = false;
    } else hovering = false;

    if (dragging){
        knobX = (mousePos.x - mouseOffset.x) - pos.x;
    }

    knobX = std::clamp(knobX, 0.f, size.x);

    sf::RectangleShape bar(size);
    bar.setOrigin(sf::Vector2f(0, size.y / 2));
    bar.setPosition(pos);
    bar.setFillColor(sf::Color(38u, 70u, 83u));
    target.draw(bar);

    // Draw knob
    sf::CircleShape knob(knobRadius);
    knob.setOrigin(sf::Vector2f(knobRadius, knobRadius));
    knob.setPosition(sf::Vector2f(pos.x + knobX, pos.y));
    knob.setFillColor(hovering || dragging ? sf::Color(235u, 94u, 85u) : sf::Color(99u, 212u, 113u));
    target.draw(knob);

    *value = min + (max - min) * (knobX / size.x);

    texts.at(2).setString(valueName + ": " + std::to_string(*value));
    for (int i = 0; i < 3; i++) target.draw(texts.at(i));
}

CheckBox::CheckBox(bool* value, std::string name, sf::Vector2f pos, sf::Vector2f size) : value(value), name(name), pos(pos), size(size) {
    if (!font.openFromFile("Fonts/OpenSans-Bold.ttf")) std::cerr << "Couldn't load slider font for " << name << std::endl; 
    const float characterSize = 11;

    text = new sf::Text(font, name + ": ", characterSize);
    text->setPosition(pos);
    text->setCharacterSize(characterSize);
    sf::FloatRect bounds = text->getLocalBounds();
    textBounds = bounds.size;
    textBounds.x += 11;
    textBounds.y = 0;
}

void CheckBox::Draw(sf::RenderTexture& target, sf::Vector2f mousePos){
    bool mouseClicked = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    sf::Vector2f checkBoxPos = pos + textBounds;
    if (mouseClicked && !mouseDown && mousePos.x > checkBoxPos.x && mousePos.x < checkBoxPos.x+size.x){
        if (mousePos.y > checkBoxPos.y && mousePos.y < checkBoxPos.y+size.y){
            *value = !*value;
        }
    }
    mouseDown = mouseClicked;

    target.draw(*text);

    sf::RectangleShape checkBox(size);
    checkBox.setPosition(checkBoxPos);
    checkBox.setFillColor(*value ? sf::Color::Green : sf::Color::Red);
    target.draw(checkBox);
}