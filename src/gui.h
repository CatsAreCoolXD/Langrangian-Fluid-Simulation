#include <SFML/Graphics.hpp>
#include <vector>

class Slider {
    public:
        Slider(float* value, float min, float max, sf::Vector2f size, sf::Vector2f pos, std::string valueName);
        void Draw(sf::RenderTexture& target, sf::Vector2f mousePos);
    private:
        float* value = nullptr;
        float min, max;
        sf::Vector2f size, pos, mouseOffset;
        bool hovering, dragging;
        float mx, knobX;

        std::string valueName;
        sf::Font font;
        std::vector<sf::Text> texts; // minText, maxText, valueText
};

class CheckBox {
    public:
        CheckBox(bool* value, std::string name, sf::Vector2f pos, sf::Vector2f size);
        ~CheckBox() { delete text; }
        void Draw(sf::RenderTexture& target, sf::Vector2f mousePos);
    private:
        bool* value = nullptr;
        sf::Vector2f pos, size;
        bool mouseDown;

        sf::Vector2f textBounds;

        std::string name;
        sf::Font font;
        sf::Text* text;
};