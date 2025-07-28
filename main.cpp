#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <tuple>
#include <array>
#include <unordered_map>
#include <imgui.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_impl_sdl3.h>
#include <string>
#include <regex>
#include <filesystem>
#include <variant>
#include <fstream>
#include <sstream>

// --- CONFIG ---

struct IDmap {
    std::unordered_map<int, std::tuple<int, int, int>> id_map;
    std::string name;
};

IDmap load_id_file(const std::string& filename) {
    IDmap map_;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Debug::OpenFile::Error::" << filename << std::endl;
        return map_;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int id, r, g, b;

        if (!(iss >> id >> r >> g >> b)) {
            std::cerr << "Debug::MalformedLine::[ " << line << std::endl;
            continue;
        }

        map_.id_map[id] = std::make_tuple(r, g, b);
    }

    file.close();
    return map_;
}

const ImVec4 info_color = {0.4f, 0.6f, 1.0f, 1.0f};
const ImVec4 warning_color = {1.0f, 0.8f, 0.3f, 1.0f};
const ImVec4 error_color = {1.0f, 0.3f, 0.3f, 1.0f};

const int SCREEN_WIDTH = 1280; // Width of the window
const int SCREEN_HEIGHT = 720; // Height of the window

int current_window_width = SCREEN_WIDTH; // Current width of the window, used for rendering
int current_window_height = SCREEN_HEIGHT; // Current height of the window, used for rendering

static bool dragging = false; // Flag to indicate if the user is dragging the mouse
static float lastMouseX = 0.0f, lastMouseY = 0.0f; // Last mouse coordinates before dragging starts 

inline void SetPixelGlobal(Uint32* pixels, int pitch, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    Uint32 color = (Uint32(r) << 24) | (Uint32(g) << 16) | (Uint32(b) << 8) | Uint32(a);
    pixels[y * (pitch / sizeof(Uint32)) + x] = color;
}

inline void SetPixelLocal(Uint32* pixels, int pitch, Uint8 r, Uint8 g, Uint8 b) {
    Uint32 color = (Uint32(r) << 24) | (Uint32(g) << 16) | (Uint32(b) << 8) | Uint32(255);
    pixels[0] = color;
}

double distanceSquared(double x1, double y1, double x2, double y2) {
    return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
}

struct IconBase {
    virtual ~IconBase() = default;

    virtual SDL_FPoint GetPosition() const = 0;
    virtual int GetIconId() const = 0;
    virtual std::tuple<int, int> GetSize() const = 0;

    virtual void SetPosition(float x_, float y_) = 0;
    virtual void SetIconId(int id) = 0;

    virtual void set_texture(SDL_Renderer* renderer, std::unordered_map<int, std::string>& id_map) = 0;
    virtual void add_decorator(SDL_Renderer* renderer, int id, std::unordered_map<int, std::string>& id_map) = 0;
    virtual void render_to_view(SDL_Renderer* renderer, SDL_FRect* viewport_output, float scale_offset, float pan_offset_x, float pan_offset_y) = 0;
    virtual void clear_decorators() = 0;
};

struct IconDecorator{
    SDL_Texture* decorator_texture;
    float width, height;
    int id;
};

struct IconCivilian : public IconBase {
    SDL_Texture* icon_texture;
    SDL_FPoint position;
    SDL_FPoint center;
    float width, height;
    float scale=1.0f;
    int icon_id;

    SDL_FRect viewport_local;

    std::tuple<int, int> GetSize() const override {
        std::tuple<int, int> output = {width, height};
        return output;
    }

    SDL_FPoint GetPosition() const override { return position; }

    int GetIconId() const override { return icon_id; }

    void SetPosition(float x_, float y_) {
        position.x = x_;
        position.y = y_;
    } 

    void SetIconId(int id) { icon_id = id; }

    void set_texture(SDL_Renderer* renderer, std::unordered_map<int, std::string>& id_map){
        if(icon_id){
            auto found_item = id_map.find(icon_id);
            if (found_item != id_map.end()) {
                std::string found_name = found_item->second;
                std::string filename_string = "icons/civilian/" + std::to_string(icon_id) + "_" + found_name + ".png";
                const char* char_buffer = filename_string.c_str();
                SDL_Surface* img_surface = IMG_Load(char_buffer);
                if (!img_surface) {
                    std::cerr << "IMG_Load failed for " << filename_string << ": " << SDL_GetError() << std::endl;
                    return;
                }
                SDL_Texture* tex_buffer = SDL_CreateTextureFromSurface(renderer, img_surface);
                if (!tex_buffer) {
                    std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
                    SDL_DestroySurface(img_surface);  // always free surface if texture creation fails
                    return;
                }
                icon_texture = tex_buffer;
                SDL_SetTextureScaleMode(icon_texture, SDL_SCALEMODE_NEAREST);
                width = img_surface->w;
                height = img_surface->h;
                SDL_DestroySurface(img_surface);
            } else {
                std::cout<<"Icon id couldn't be found.";
            }
        } else {
            std::cout<<"Icon id is undefined, no texture will be assigned.";
        }
    }

    void add_decorator(SDL_Renderer* renderer, int id, std::unordered_map<int, std::string>& id_map){
        std::cout<<"Civilian doesn't have decorator, skipping."<<std::endl;
    }

    void clear_decorators(){
        std::cout<<"Civilian doesn't have decorator, skipping."<<std::endl;
    }

    void render_to_view(SDL_Renderer* renderer, SDL_FRect* viewport_output, float scale_offset, float pan_offset_x, float pan_offset_y){
        scale = fmaxf(1.0f-(scale_offset/3), 0.05f);

        float position_x_scaled = (float)position.x * scale_offset;
        float position_y_scaled = (float)position.y * scale_offset;

        float pan_x_scaled = (float)pan_offset_x * scale_offset;
        float pan_y_scaled = (float)pan_offset_y * scale_offset;

        float width_scaled = (float)(width*scale) * scale_offset;
        float height_scaled = (float)(height*scale) * scale_offset;

        viewport_local.x = position_x_scaled - pan_x_scaled - width_scaled / 2.0f;
        viewport_local.y = position_y_scaled - pan_y_scaled - height_scaled / 2.0f;
        viewport_local.w = width_scaled;
        viewport_local.h = height_scaled;
        SDL_RenderTexture(renderer, icon_texture, NULL, &viewport_local);
    }
};

struct IconMilitary : public IconBase {
    SDL_Texture* icon_texture;
    SDL_Texture* decorator_texture;
    SDL_FPoint position;
    SDL_FPoint center;
    float width, height, scale=1.0f;
    int icon_id, country_id, quality;
    double angle=0.0;
    std::vector<IconDecorator> decorators;

    SDL_FRect viewport_local;
    SDL_FRect viewport_local_decorator;

    std::tuple<int, int> GetSize() const override {
        std::tuple<int, int> output = {width, height};
        return output;
    }

    SDL_FPoint GetPosition() const override { return position; }

    int GetIconId() const override {
        return icon_id;
    }

    void SetPosition(float x_, float y_) {
        position.x = x_;
        position.y = y_;
    } 

    void SetIconId(int id) { icon_id = id; }

    void set_texture(SDL_Renderer* renderer, std::unordered_map<int, std::string>& id_map){
        if(icon_id){
            auto found_item = id_map.find(icon_id);
            if (found_item != id_map.end()) {
                std::string found_name = found_item->second;
                std::string filename_string = "icons/military/" + std::to_string(icon_id) + "_" + found_name + ".png";
                const char* char_buffer = filename_string.c_str();
                SDL_Surface* img_surface = IMG_Load(char_buffer);
                if (!img_surface) {
                    std::cerr << "IMG_Load failed for " << filename_string << ": " << SDL_GetError() << std::endl;
                    return;
                }
                SDL_Texture* tex_buffer = SDL_CreateTextureFromSurface(renderer, img_surface);
                if (!tex_buffer) {
                    std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
                    SDL_DestroySurface(img_surface);  // always free surface if texture creation fails
                    return;
                }
                icon_texture = tex_buffer;
                SDL_SetTextureScaleMode(icon_texture, SDL_SCALEMODE_NEAREST);
                width = img_surface->w;
                height = img_surface->h;
                SDL_DestroySurface(img_surface);
            } else {
                std::cout<<"Icon id couldn't be found.";
            }
        } else {
            std::cout<<"Icon id is undefined, no texture will be assigned.";
        }
    }

    void add_decorator(SDL_Renderer* renderer, int id, std::unordered_map<int, std::string>& id_map){
        auto found_item = id_map.find(id);
        if (found_item != id_map.end()) {
            std::string found_name = found_item->second;
            std::string filename_string = "icons/decorator/" + std::to_string(id) + "_" + found_name + ".png";
            const char* char_buffer = filename_string.c_str();
            SDL_Surface* img_surface = IMG_Load(char_buffer);
            if (!img_surface) {
                std::cerr << "IMG_Load failed for " << filename_string << ": " << SDL_GetError() << std::endl;
                return;
            }
            SDL_Texture* tex_buffer = SDL_CreateTextureFromSurface(renderer, img_surface);
            if (!tex_buffer) {
                std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
                SDL_DestroySurface(img_surface);  // always free surface if texture creation fails
                return;
            }
            SDL_SetTextureScaleMode(tex_buffer, SDL_SCALEMODE_NEAREST);
            IconDecorator decorator;
            decorator.decorator_texture = tex_buffer;
            decorator.id = id;
            decorator.width = img_surface->w;
            decorator.height = img_surface->h;
            SDL_DestroySurface(img_surface);

            decorators.push_back(decorator);
        } else {
            std::cout<<"Decorator id couldn't be found.";
        }
    }

    void clear_decorators(){
        decorators.clear();
    }

    void render_to_view(SDL_Renderer* renderer, SDL_FRect* viewport_output, float scale_offset, float pan_offset_x, float pan_offset_y){
        scale = fmaxf(1.0f-(scale_offset/3), 0.05f);

        float position_x_scaled = (float)position.x * scale_offset;
        float position_y_scaled = (float)position.y * scale_offset;

        float pan_x_scaled = (float)pan_offset_x * scale_offset;
        float pan_y_scaled = (float)pan_offset_y * scale_offset;

        float width_scaled = (float)(width*scale) * scale_offset;
        float height_scaled = (float)(height*scale) * scale_offset;

        viewport_local.x = position_x_scaled - pan_x_scaled - width_scaled / 2.0f;
        viewport_local.y = position_y_scaled - pan_y_scaled - height_scaled / 2.0f;
        viewport_local.w = width_scaled;
        viewport_local.h = height_scaled;

        center = { viewport_local.w * 0.5f, viewport_local.h * 0.5f };
        SDL_RenderTextureRotated(renderer, icon_texture, NULL, &viewport_local, angle, &center, SDL_FLIP_NONE);

        int index = 1;
        for (auto& decorator_icon : decorators) {
            viewport_local_decorator.x = position_x_scaled - pan_x_scaled - width_scaled / 2.0f;
            viewport_local_decorator.y = (position_y_scaled - pan_y_scaled - height_scaled / 2.0f)-height_scaled*index;
            viewport_local_decorator.w = width_scaled;
            viewport_local_decorator.h = height_scaled;

            SDL_RenderTexture(renderer, decorator_icon.decorator_texture, NULL, &viewport_local_decorator);
            index++;
        }

    }
};

struct IconMarker : public IconBase {
    SDL_Texture* icon_texture;
    SDL_FPoint position;
    float width, height, scale=1.0f;
    int icon_id;
};

struct Shape {
    int size = 0;
    int capacity = 2;
    SDL_FPoint* point_array;
    uint8_t r=255, g=255, b=255, a=255;

    Shape() {
        point_array = new SDL_FPoint[capacity];
    }

    Shape(const Shape& other) {
        size = other.size;
        capacity = other.capacity;
        r = other.r;
        g = other.g;
        b = other.b;
        a = other.a;
        point_array = new SDL_FPoint[capacity];
        for (int i = 0; i < size; ++i) {
            point_array[i] = other.point_array[i];
        }
    }

    Shape& operator=(const Shape& other) {
        if (this == &other) return *this;
        delete[] point_array;

        size = other.size;
        capacity = other.capacity;
        point_array = new SDL_FPoint[capacity];
        for (int i = 0; i < size; ++i) {
            point_array[i] = other.point_array[i];
        }

        return *this;
    }

    void AddPoint(SDL_FPoint position) {
        if (size >= capacity) {
            int new_capacity = capacity * 2;
            SDL_FPoint* new_array = new SDL_FPoint[new_capacity];

            for (int i = 0; i < size; ++i) {
                new_array[i] = point_array[i];
            }

            delete[] point_array;
            point_array = new_array;
            capacity = new_capacity;
        }

        point_array[size++] = position;
    }

    const SDL_FPoint* GetPoints() const {
        return point_array;
    }

    int GetSize() { return size; }

    void ClearPoints() {
        delete[] point_array;
        capacity = 2;
        size = 0;
        point_array = new SDL_FPoint[capacity];
    }

    ~Shape() {
        delete[] point_array;
    }
};

struct IconLayer{
    std::string layer_name;
    bool visible = true;

    std::vector<IconCivilian> IconsCivilian;
    std::vector<IconMilitary> IconsMilitary;
    // std::vector<IconMarker> IconsMarker;
    std::vector<Shape> Shapes;

    IconCivilian& create_civilian_icon(SDL_Renderer* renderer, int icon_id, float pos_x, float pos_y, std::unordered_map<int, std::string>& idmap){
        IconCivilian icon;
        icon.icon_id = icon_id;
        icon.position.x = pos_x;
        icon.position.y = pos_y;
        icon.set_texture(renderer, idmap);
        IconsCivilian.push_back(icon);

        return IconsCivilian.back();
    }

    IconMilitary& create_military_icon(SDL_Renderer* renderer, int icon_id, float pos_x, float pos_y, std::unordered_map<int, std::string>& idmap){
        IconMilitary icon;
        icon.icon_id = icon_id;
        icon.position.x = pos_x;
        icon.position.y = pos_y;
        icon.set_texture(renderer, idmap);
        IconsMilitary.push_back(icon);

        return IconsMilitary.back();
    }

    void create_shape(Shape shape, Uint8 r = 255, Uint8 g = 255, Uint8 b = 255, Uint8 a = 255){
        if(shape.GetSize()>=2){
            shape.r = r;
            shape.g = g;
            shape.b = b;
            shape.a = a;
            Shapes.push_back(shape);
            std::cout<<"Debug::Shape::PushBack"<<std::endl;
        } else {
            std::cout<<"Debug::Shape::PushBack::Error"<<std::endl;
        }
    }
};

struct WorldLayer{
    std::string layer_name;
    bool visible = true;
    bool is_upper;

    SDL_Texture* layer_texture;
};

IconCivilian* FindClosestCivilianIcon(std::vector<IconCivilian>& vector_items, double targetX, double targetY) {
    double minDist = std::numeric_limits<double>::max();
    IconCivilian* closest_icon = nullptr;

    for (auto& icon : vector_items) {
        double dist = distanceSquared(icon.position.x, icon.position.y, targetX, targetY);
        if (dist < minDist) {
            minDist = dist;
            closest_icon = &icon;
        }
    }

    return closest_icon;
}

class World{
    private:
        std::vector<IconLayer> IconLayers;
        std::vector<WorldLayer> WorldLayers;
        std::string last_created_layer_name = "undefined";

    public:
        bool WORLD_HAS_INITIALIZED = false;

        int UPPER_WORLD_WIDTH=0; // number of chunks along the X-axis, i.e bigger sections of tiles
        int UPPER_WORLD_HEIGHT=0; // number of chunks along the Y-axis, i.e bigger sections of tiles
        int CHUNK_WIDTH=0; // Number of tiles in a chunk along the X-axis
        int CHUNK_HEIGHT=0; // Number of tiles in a chunk along the Y-axis
        int LOWER_WORLD_WIDTH=0; // Total number of tiles along the X-axis
        int LOWER_WORLD_HEIGHT=0; // Total number of tiles along the Y-axis

        float TILE_LENGTH; // Length of a tile in kilometers, used for measurements and grasping scale
        float WORLD_LENGTH; // Total equatorial length of the world in kilometers

        std::vector<IDmap> IDmaps;

        // std::unordered_map<int, std::tuple<int, int, int>> TILE_ID_MAP = {
        //     {255, {0, 0, 0}},       // CLEAR TILE [ NONE ]
        //     {0, {0, 0, 127}},       // WATER
        //     {1, {99, 173, 95}},     // COLD PLAINS
        //     {2, {52, 72, 40}},      // BOREAL FOREST
        //     {3, {10, 87, 6}},       // DECIDUOUS FOREST
        //     {4, {16, 59, 17}},      // CONIFEROUS FOREST
        //     {5, {64, 112, 32}},     // TROPICAL FOREST
        //     {6, {80, 96, 48}},      // SWAMPLAND
        //     {7, {7, 154, 0}},       // PLAINS
        //     {8, {12, 172, 0}},      // PRAIRIE
        //     {9, {124, 156, 0}},     // SAVANNA
        //     {10, {80, 80, 64}},     // MARSHLAND
        //     {11, {64, 80, 80}},     // MOOR
        //     {12, {112, 112, 64}},   // STEPPE
        //     {13, {64, 64, 16}},     // TUNDRA
        //     {14, {255, 186, 0}},    // MAGMA
        //     {15, {112, 80, 96}},    // CANYONS
        //     {16, {132, 132, 132}},  // MOUNTAINS
        //     {17, {112, 112, 96}},   // STONE DESERT
        //     {18, {64, 64, 57}},     // CRAGS
        //     {19, {192, 192, 192}},  // SNOWLANDS
        //     {20, {224, 224, 224}},  // ICE PLAINS
        //     {21, {112, 112, 32}},   // BRUSHLAND
        //     {22, {253, 157, 24}},   // RED SANDS
        //     {23, {238, 224, 192}},  // SALT FLATS
        //     {24, {255, 224, 160}},  // COASTAL DESERT
        //     {25, {255, 208, 144}},  // DESERT
        //     {26, {128, 64, 0}},     // WETLAND
        //     {27, {59, 29, 10}},     // MUDLAND
        //     {28, {84, 65, 65}},     // HIGHLANDS/FOOTHILLS
        //     {29, {170, 153, 153}},  // ABYSSAL WASTE
        //     {30, {182, 170, 191}},  // PALE WASTE
        //     {31, {51, 102, 153}},   // ELYSIAN FOREST
        //     {32, {10, 59, 59}},     // ELYSIAN JUNGLE
        //     {33, {203, 99, 81}},    // VOLCANIC WASTES
        //     {34, {121, 32, 32}},    // IGNEOUS ROCKLAND
        //     {35, {59, 10, 10}},     // CRIMSON FOREST
        //     {36, {192, 176, 80}},   // FUNGAL FOREST
        //     {37, {153, 204, 0}},    // SULFURIC FIELDS
        //     {38, {240, 240, 187}},  // LIMESTONE DESERT
        //     {39, {255, 163, 255}},  // DIVINE FIELDS
        //     {40, {170, 48, 208}},   // DIVINE MEADOW
        //     {41, {117, 53, 144}},   // DIVINE WOODLAND
        //     {42, {102, 32, 137}}    // DIVINE EDEN
        // };

        std::unordered_map<int, std::string> CivilianIdMap;
        std::unordered_map<int, std::string> MilitaryIdMap;
        std::unordered_map<int, std::string> MarkerIdMap;
        std::unordered_map<int, std::string> DecoratorIdMap;
        
        IconBase* selected_world_icon = nullptr;

        bool HasInitializedCheck() {
            if(UPPER_WORLD_HEIGHT>0 && CHUNK_WIDTH>0){
                return true;
            } else {
                return false;
            }
        }

        void toggle_visibility_layer(const std::string& name_id) {
            if(get_layer_type(name_id)){
                WorldLayer& it = get_worldlayer(name_id);
                it.visible = !it.visible;
                std::cout<<"Debug::WorldLayer::Visibility::"<<it.visible<<std::endl;
            } else {
                IconLayer& it = get_iconlayer(name_id);
                it.visible = !it.visible;
                std::cout<<"Debug::IconLayer::Visibility::"<<it.visible<<std::endl;
            }
        }

        std::vector<IconLayer>& GetIconLayers() { return IconLayers; }

        std::vector<WorldLayer>& GetWorldLayers() { return WorldLayers; }

        void MoveIconLayer(size_t index, bool down) {
            if(down){
                if (index == 0 || index >= IconLayers.size()) return;
                std::swap(IconLayers[index], IconLayers[index - 1]);
            } else {
                if (index >= IconLayers.size() - 1) return;
                std::swap(IconLayers[index], IconLayers[index + 1]);
            }
        }

        void MoveWorldLayer(size_t index, bool down) {
            if(down){
                if (index == 0 || index >= WorldLayers.size()) return;
                std::swap(WorldLayers[index], WorldLayers[index - 1]);
            } else {
                if (index >= WorldLayers.size() - 1) return;
                std::swap(WorldLayers[index], WorldLayers[index + 1]);
            }
        }

        void discover_icons() {
            std::regex pattern(R"((\d+)_([a-zA-Z0-9]+)\.(png))");

            for (const auto &entry : std::filesystem::directory_iterator("icons/civilian")) {
                if (!entry.is_regular_file())
                    continue;

                std::string filename = entry.path().filename().string();
                std::smatch matches;
                
                if (std::regex_match(filename, matches, pattern)) {
                    int id = std::stoi(matches[1].str());
                    std::string name = matches[2].str();
                    CivilianIdMap[id] = name;
                }
            }
            for (const auto &[id, name] : CivilianIdMap) {
                std::cout << "Civilian ID: " << id << " => Name: " << name << std::endl;
            }

            for (const auto &entry : std::filesystem::directory_iterator("icons/military")) {
                if (!entry.is_regular_file())
                    continue;

                std::string filename = entry.path().filename().string();
                std::smatch matches;
                
                if (std::regex_match(filename, matches, pattern)) {
                    int id = std::stoi(matches[1].str());
                    std::string name = matches[2].str();
                    MilitaryIdMap[id] = name;
                }
            }
            for (const auto &[id, name] : MilitaryIdMap) {
                std::cout << "Military ID: " << id << " => Name: " << name << std::endl;
            }

            for (const auto &entry : std::filesystem::directory_iterator("icons/markers")) {
                if (!entry.is_regular_file())
                    continue;

                std::string filename = entry.path().filename().string();
                std::smatch matches;
                
                if (std::regex_match(filename, matches, pattern)) {
                    int id = std::stoi(matches[1].str());
                    std::string name = matches[2].str();
                    MarkerIdMap[id] = name;
                }
            }
            for (const auto &[id, name] : MarkerIdMap) {
                std::cout << "Marker ID: " << id << " => Name: " << name << std::endl;
            }

            for (const auto &entry : std::filesystem::directory_iterator("icons/decorator")) {
                if (!entry.is_regular_file())
                    continue;

                std::string filename = entry.path().filename().string();
                std::smatch matches;
                
                if (std::regex_match(filename, matches, pattern)) {
                    int id = std::stoi(matches[1].str());
                    std::string name = matches[2].str();
                    DecoratorIdMap[id] = name;
                }
            }
            for (const auto &[id, name] : DecoratorIdMap) {
                std::cout << "Decorator ID: " << id << " => Name: " << name << std::endl;
            }
        }

        void discover_ids() {
            std::regex pattern(R"((\d+)_([a-zA-Z0-9]+)\.(txt))");

            for (const auto &entry : std::filesystem::directory_iterator("idmaps")) {
                if (!entry.is_regular_file())
                    continue;

                std::string filename = entry.path().filename().string();
                std::smatch matches;
                
                if (std::regex_match(filename, matches, pattern)) {
                    int id = std::stoi(matches[1].str());
                    std::string name = matches[2].str();

                    IDmap map_ = load_id_file("idmaps/"+filename);
                    map_.name = name;

                    IDmaps.push_back(map_);
                }
            }
            for (auto& map_ : IDmaps) {
                std::cout << "Map: " << map_.name << std::endl;
            }
        }

        WorldLayer& create_worldlayer(SDL_Renderer* renderer, const std::string& name_id, bool is_upper) {
            int width, height;
            if (is_upper) {
                width = UPPER_WORLD_WIDTH;
                height = UPPER_WORLD_HEIGHT;
            } else {
                width = LOWER_WORLD_WIDTH;
                height = LOWER_WORLD_HEIGHT;
            }

            if (width <= 0 || height <= 0) {
                std::cerr << "Invalid texture size: " << width << " x " << height << std::endl;
                throw std::runtime_error("Layer not found");
            }

            SDL_Texture *layer_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);

            if (!layer_texture) {
                SDL_Log("Failed to create texture '%s': %s", name_id.c_str(), SDL_GetError());
                throw std::runtime_error("Layer not found");
            }

            SDL_SetTextureScaleMode(layer_texture, SDL_SCALEMODE_NEAREST);

            WorldLayer world_layer;
            world_layer.is_upper = is_upper;
            if(name_id.length()!=0){
                world_layer.layer_name = name_id;
            } else {
                world_layer.layer_name = last_created_layer_name+"_a";
            }
            world_layer.layer_texture = layer_texture;

            WorldLayers.push_back(world_layer);
            std::cout<<"Debug::Created::WorldLayer::"<<world_layer.layer_name<<std::endl;
            last_created_layer_name = world_layer.layer_name;
            return WorldLayers.back();
        }

        IconLayer& create_iconlayer(const std::string& name_id) {
            IconLayer icon_layer;
            if(name_id.length()!=0){
                icon_layer.layer_name = name_id;
            } else {
                icon_layer.layer_name = last_created_layer_name+"_a";
            }
            IconLayers.push_back(icon_layer);
            std::cout<<"Debug::Created::IconLayer::"<<icon_layer.layer_name<<std::endl;
            last_created_layer_name = icon_layer.layer_name;
            return IconLayers.back();
        }

        IconLayer& get_iconlayer(const std::string& name_id) {
            for (auto& layer : IconLayers) {
                if (layer.layer_name == name_id) {
                    return layer;
                }
            }

            throw std::runtime_error("Layer not found");
        }

        WorldLayer& get_worldlayer(const std::string& name_id) {
            for (auto& layer : WorldLayers) {
                if (layer.layer_name == name_id) {
                    return layer;
                }
            }

            throw std::runtime_error("Layer not found");
        }

        void remove_worldlayer(const std::string& name_id) {
            for (auto it = WorldLayers.begin(); it != WorldLayers.end(); ) {
                if (it->layer_name == name_id) {
                    it = WorldLayers.erase(it);
                    std::cout<<"Debug::Erased::Worldlayer::"<<it->layer_name<<std::endl;
                } else {
                    std::cout<<"Debug::CheckingVector::CurrentVectorIterator::"<<it->layer_name<<std::endl;
                    ++it;
                }
            }
        }

        void remove_iconlayer(const std::string& name_id) {
            for (auto it = IconLayers.begin(); it != IconLayers.end(); ) {
                if (it->layer_name == name_id) {
                    it = IconLayers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        bool get_layer_type(const std::string& name_id) {
            for (auto& layer : WorldLayers) {
                if (layer.layer_name == name_id) {
                    return true;
                }
            }
            for (auto& layer : IconLayers) {
                if (layer.layer_name == name_id) {
                    return false;
                }
            }

            throw std::runtime_error("Layer not found");
        }

        void set_world_size(int w, int h) {
            UPPER_WORLD_WIDTH = w;
            UPPER_WORLD_HEIGHT = h;
            std::cout<<"Set world to "<<w<<" "<<h<<std::endl;
        }

        std::tuple<int, int> get_world_size(bool is_upper) {
            if (is_upper) {
                if (UPPER_WORLD_WIDTH > 0 && UPPER_WORLD_HEIGHT > 0) {
                    return {UPPER_WORLD_WIDTH, UPPER_WORLD_HEIGHT};
                } else {
                    printf("World doesn't have defined size yet.\n");
                    return {0, 0}; // default
                }
            } else {
                if (UPPER_WORLD_WIDTH > 0 && UPPER_WORLD_HEIGHT > 0 && CHUNK_WIDTH > 0 && CHUNK_HEIGHT > 0) {
                    return {UPPER_WORLD_WIDTH * CHUNK_WIDTH, UPPER_WORLD_HEIGHT * CHUNK_HEIGHT};
                } else {
                    printf("World doesn't have defined size or chunk size yet.\n");
                    return {0, 0}; // default
                }
            }
        }

        void set_chunk_size(int w, int h) {
            CHUNK_WIDTH = w;
            CHUNK_HEIGHT = h;

            LOWER_WORLD_WIDTH = UPPER_WORLD_WIDTH*CHUNK_WIDTH;
            LOWER_WORLD_HEIGHT = UPPER_WORLD_HEIGHT*CHUNK_HEIGHT;
            std::cout<<"Set chunks to "<<w<<" "<<h<<" resulting in lower "<<LOWER_WORLD_WIDTH<<" "<<LOWER_WORLD_HEIGHT<<std::endl;
        }

        std::tuple<int,int> get_chunk_size() {
            if(CHUNK_WIDTH>0 && CHUNK_HEIGHT>0){
                std::tuple<int, int> chunk_size(CHUNK_WIDTH, CHUNK_HEIGHT);
                return chunk_size;
            } else {
                printf("World doesn't have defined chunk size yet.");
                return {0, 0}; // Default/fallback
            }
        }

        void draw_all(SDL_Renderer* renderer, SDL_FRect* input_viewport_lower, SDL_FRect* input_viewport_upper, SDL_FRect* output_viewport, float scale_offset, float pan_offset_x, float pan_offset_y) {
            for (auto& layer : WorldLayers) {
                const std::string& name = layer.layer_name;
                SDL_Texture* texture = layer.layer_texture;

                if(layer.visible){
                    if(layer.is_upper){
                        SDL_RenderTexture(renderer, texture, input_viewport_upper, output_viewport);
                    } else {
                        SDL_RenderTexture(renderer, texture, input_viewport_lower, output_viewport);
                    }
                }
            };
            for (auto& icon_layer : IconLayers) {
                const std::string& name = icon_layer.layer_name;
                Uint8 r_, g_, b_, a_;

                if(icon_layer.visible){
                    for (auto& shape : icon_layer.Shapes) {
                        const SDL_FPoint* points = shape.GetPoints();
                        const int size_of_array = shape.GetSize();

                        std::vector<SDL_FPoint> screen_points(size_of_array);
                        for (int i = 0; i < size_of_array; ++i) {
                            screen_points[i].x = (points[i].x - pan_offset_x) * scale_offset;
                            screen_points[i].y = (points[i].y - pan_offset_y) * scale_offset;
                        }

                        SDL_GetRenderDrawColor(renderer, &r_, &g_, &b_, &a_);
                        // honestly don't know if it's even efficient to do it this way, of saving and re-using the pointers;

                        SDL_SetRenderDrawColor(renderer, shape.r, shape.g, shape.b, shape.a);

                        if (SDL_RenderLines(renderer, screen_points.data(), size_of_array) < 0) {
                            std::cerr << "Debug::RenderLines::Error::" << SDL_GetError() << std::endl;
                        }

                        SDL_SetRenderDrawColor(renderer, r_, g_, b_, a_);
                    }

                    for (auto& icon : icon_layer.IconsCivilian) {
                        icon.render_to_view(renderer, output_viewport, scale_offset, pan_offset_x, pan_offset_y);
                    }
                    for (auto& icon : icon_layer.IconsMilitary) {
                        icon.render_to_view(renderer, output_viewport, scale_offset, pan_offset_x, pan_offset_y);
                    }
                    // for (auto& icon : icon_layer.IconsMarker) {
                    //     icon.render_to_view(renderer, output_viewport, scale_offset, pan_offset_x, pan_offset_y);
                    // }
                };
            };
        };
    };

int main(int argc, char* args[]) {
    // Initialize SDL
    bool SDL_initialized = SDL_Init(SDL_INIT_VIDEO);
    if (!SDL_initialized) {
        SDL_Log("Initialization could not be done! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create a window 
    SDL_Window* window = SDL_CreateWindow("NATIONWIDER C++", SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create a renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_Log("Could not create renderer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // IMGUI
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    // ---

    Uint32* pixels;
    int pitch;

    World world;
    world.discover_icons();
    world.discover_ids();
    int world_width_lower=1, world_height_lower=1, world_width_upper=1, world_height_upper=1, chunk_width=1, chunk_height=1;

    // ---

    // IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    // ---

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    // ---

    // constants for navigation
    std::string selected_layer;
    std::string selected_linetool;
    std::string selected_idmap;
    ImVec4 line_color;
    Shape temporary_shape;
    SDL_FPoint last_clicked_position = {0, 0};
    bool pressed_first_line = false;
    int selected_decorator_id;
    bool moving_icon = false;
    bool rotating_icon = false;
    bool editing_map = false;
    int selected_icon_id;
    int selected_icon_class;
    int selected_tile_id = 0;
    float size_offset = 1.0f;
    float pan_offset_x = 0.0f;
    float pan_offset_y = 0.0f;
    bool popup = false;

    SDL_FRect texture_rect = {0, 0, (float)world_width_lower, (float)world_height_lower};
    SDL_FRect intersect;
    SDL_Rect lockRect;
    SDL_FRect viewport_output_bounded;
    SDL_FRect viewport_source_lower;
    SDL_FRect viewport_source_upper;
    SDL_FRect viewport_output;
    viewport_output.x = 0; viewport_output.y = 0;
    viewport_output.w = current_window_width;
    viewport_output.h = current_window_height;
    // ---

    // Main loop
    bool quit = false;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            // IMGUI
            ImGui_ImplSDL3_ProcessEvent(&e);
            bool dont_let_passthrough = false;
            // ---

            // Handle quit event
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE)) {
                quit = true;
            }

            auto& io = ImGui::GetIO();
            if (io.WantCaptureMouse || io.WantCaptureKeyboard) {
                dont_let_passthrough = true; // Skip processing events if ImGui is capturing them
            }

            // <float> mouse coordinates in screen space
            float mouse_screenX, mouse_screenY;
            SDL_GetMouseState(&mouse_screenX, &mouse_screenY);
            // <float> mouse coordinates in world space
            float mouse_worldX, mouse_worldY;
            mouse_worldX = static_cast<float>((mouse_screenX / size_offset) + pan_offset_x);
            mouse_worldY = static_cast<float>((mouse_screenY / size_offset) + pan_offset_y);

            // <int> X texture tile coordinate
            int textureX = static_cast<int>(mouse_worldX);
            // <int> Y texture tile coordinate
            int textureY = static_cast<int>(mouse_worldY);

            int upper_textureX = static_cast<int>(mouse_worldX / chunk_width);
            int upper_textureY = static_cast<int>(mouse_worldY / chunk_height);

            int paint_color_r=255, paint_color_g=255, paint_color_b=255;

            IDmap* referenced_idmap = nullptr;
            for (auto& id_map : world.IDmaps) {
                if (id_map.name == selected_idmap) {
                    referenced_idmap = &id_map;
                    break;
                }
            }

            if (referenced_idmap) {
                auto it = referenced_idmap->id_map.find(selected_tile_id);
                if (it != referenced_idmap->id_map.end()) {
                    std::tie(paint_color_r, paint_color_g, paint_color_b) = it->second;
                } else {
                    std::cerr << "Tile ID " << selected_tile_id << " not found in ID map: " << selected_idmap << std::endl;
                }
            }

            // Handle mouse button down events
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                bool selected_layer_type;

                if(!selected_layer.empty()){
                    selected_layer_type = world.get_layer_type(selected_layer);
                }

                if (e.button.button == SDL_BUTTON_LEFT && !dont_let_passthrough) {
                    if(!selected_layer.empty()){
                        if(selected_layer_type){ // IS WORLD_LAYER
                            if(editing_map){
                                WorldLayer& referenced_layer = world.get_worldlayer(selected_layer);
                                bool is_upper_layer = referenced_layer.is_upper;

                                int locking_coordinate_x, locking_coordinate_y;
                                if(is_upper_layer){
                                    locking_coordinate_x = upper_textureX;
                                    locking_coordinate_y = upper_textureY;
                                    if (upper_textureX < 0 || upper_textureX >= world_width_upper || upper_textureY < 0 || upper_textureY >= world_height_upper) {
                                        printf("Debug::LeftClick::OutOfBounds::(%d, %d)\n", upper_textureX, upper_textureY);
                                        continue;
                                    }
                                } else {
                                    locking_coordinate_x = textureX;
                                    locking_coordinate_y = textureY;
                                    if (textureX < 0 || textureX >= world_width_lower || textureY < 0 || textureY >= world_height_lower) {
                                        printf("Debug::LeftClick::OutOfBounds::(%d, %d)\n", textureX, textureY);
                                        continue;
                                    }
                                }

                                lockRect = { locking_coordinate_x, locking_coordinate_y, 1, 1 };
                                SDL_Texture* referenced_texture = referenced_layer.layer_texture;
                                SDL_LockTexture(referenced_texture, &lockRect, (void**)&pixels, &pitch);
                                Uint32 color = (Uint32(paint_color_r) << 24) | (Uint32(paint_color_g) << 16) | (Uint32(paint_color_b) << 8) | Uint32(255);
                                pixels[0] = color;
                                SDL_UnlockTexture(referenced_texture);
                            }
                        } else { // IS ICON_LAYER
                        std::cout<<"Debug::LeftClick::SelectedLayer::Icon"<<std::endl;

                        IconLayer& referenced_layer = world.get_iconlayer(selected_layer);
                        if(selected_icon_id){
                            if(selected_icon_class==1){
                                referenced_layer.create_civilian_icon(renderer, selected_icon_id, mouse_worldX, mouse_worldY, world.CivilianIdMap);
                            }
                            if(selected_icon_class==2){
                                auto& icon_created = referenced_layer.create_military_icon(renderer, selected_icon_id, mouse_worldX, mouse_worldY, world.MilitaryIdMap);
                                if(selected_decorator_id){
                                    icon_created.add_decorator(renderer, selected_decorator_id, world.DecoratorIdMap);
                                }
                            }
                            if(selected_icon_class==3){
                                std::cout<<"Debug::Placeholder"<<std::endl;
                            }
                        }

                        if(selected_linetool == "add"){
                            SDL_FPoint temporary_point = {mouse_worldX, mouse_worldY};

                            if(pressed_first_line) {
                                temporary_shape.AddPoint(temporary_point);
                                std::cout<<"Added position to shape x:"<<mouse_worldX<<" y:"<<mouse_worldY<<std::endl;
                            } else {
                                temporary_shape.AddPoint(temporary_point);
                                pressed_first_line = true;
                                std::cout<<"Added first position to shape x:"<<mouse_worldX<<" y:"<<mouse_worldY<<std::endl;
                            }
                        }

                        IconCivilian* closest_civilian = nullptr;
                        IconMilitary* closest_military = nullptr;
                        double MinDist = 2048.0;
                        // ---
                        for (auto& layer : world.GetIconLayers()) {
                            for (auto& CivilianLayerIcon : layer.IconsCivilian) {
                                double distance = distanceSquared(CivilianLayerIcon.position.x, CivilianLayerIcon.position.y, textureX, textureY);
                                if(distance < MinDist){
                                    MinDist = distance;
                                    closest_civilian = &CivilianLayerIcon;
                                    closest_military = nullptr;
                                }
                            }
                            for (auto& MilitaryLayerIcon : layer.IconsMilitary) {
                                double distance = distanceSquared(MilitaryLayerIcon.position.x, MilitaryLayerIcon.position.y, textureX, textureY);
                                if(distance < MinDist){
                                    MinDist = distance;
                                    closest_civilian = nullptr;
                                    closest_military = &MilitaryLayerIcon;
                                }
                            }
                        }
                        
                        if (closest_civilian != nullptr){ 
                            world.selected_world_icon = closest_civilian;
                        } else if (closest_military != nullptr){ 
                            world.selected_world_icon = closest_military; 
                        } else {
                            world.selected_world_icon = nullptr;
                        }

                        }

                    } else {
                        std::cout<<"Debug::LeftClick::SelectedLayer::None"<<std::endl;
                    }

                    if (world.selected_world_icon && !editing_map) {
                        SDL_FPoint aiririai = world.selected_world_icon->GetPosition();
                        std::cout << selected_icon_id << " " << selected_icon_class
                                << " x=" << aiririai.x << " y=" << aiririai.y << std::endl;
                    } else {
                        std::cout << "No icon selected.\n";
                    }

                }
                if (e.button.button == SDL_BUTTON_RIGHT) {
                    if(!selected_layer.empty()){
                        if(!selected_layer_type){
                            if(pressed_first_line){
                                IconLayer& referenced_layer = world.get_iconlayer(selected_layer);
                                int r=255, g=255, b=255;
                                if(selected_tile_id){
                                    auto it = referenced_idmap->id_map.find(selected_tile_id);
                                    std::tie(r, g, b) = it->second;
                                }
                                referenced_layer.create_shape(temporary_shape, r, g, b, 255);
                                pressed_first_line = false;
                                temporary_shape.ClearPoints();
                            }
                        }
                    }

                    selected_icon_id = 0;
                    popup = !popup;
                    dragging = true;
                    lastMouseX = e.button.x;
                    lastMouseY = e.button.y;
                }
            }

            // Handle mouse drag and motion events
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (e.button.button == SDL_BUTTON_LEFT && !dont_let_passthrough) {
                    if(!selected_layer.empty()){
                        bool selected_layer_type = world.get_layer_type(selected_layer);
                        if(selected_layer_type){ // IS WORLD_LAYER
                            if(editing_map){
                                WorldLayer referenced_layer = world.get_worldlayer(selected_layer);
                                bool is_upper_layer = referenced_layer.is_upper;

                                int locking_coordinate_x, locking_coordinate_y;
                                if(is_upper_layer){
                                    locking_coordinate_x = upper_textureX;
                                    locking_coordinate_y = upper_textureY;
                                    if (upper_textureX < 0 || upper_textureX >= world_width_upper || upper_textureY < 0 || upper_textureY >= world_height_upper) {
                                        printf("Debug::LeftClick::OutOfBounds::(%d, %d)\n", upper_textureX, upper_textureY);
                                        continue;
                                    }
                                } else {
                                    locking_coordinate_x = textureX;
                                    locking_coordinate_y = textureY;
                                    if (textureX < 0 || textureX >= world_width_lower || textureY < 0 || textureY >= world_height_lower) {
                                        printf("Debug::LeftClick::OutOfBounds::(%d, %d)\n", textureX, textureY);
                                        continue;
                                    }
                                }

                                lockRect = { locking_coordinate_x, locking_coordinate_y, 1, 1 };
                                SDL_Texture* referenced_texture = referenced_layer.layer_texture;
                                SDL_LockTexture(referenced_texture, &lockRect, (void**)&pixels, &pitch);
                                Uint32 color = (Uint32(paint_color_r) << 24) | (Uint32(paint_color_g) << 16) | (Uint32(paint_color_b) << 8) | Uint32(255);
                                pixels[0] = color;
                                SDL_UnlockTexture(referenced_texture);
                            }
                        } else { // IS ICON_LAYER
                            std::cout<<"Debug::LeftClickMotion::SelectedLayer::Icon"<<std::endl;
                            if(world.selected_world_icon && moving_icon){
                                auto [icon_width, icon_height] = world.selected_world_icon->GetSize();
                                world.selected_world_icon->SetPosition(textureX-(float)icon_width/2, textureY-(float)icon_height/2);
                            }
                        }
                    } else {
                        std::cout<<"Debug::LeftClickMotion::SelectedLayer::None"<<std::endl;
                    }
                }
                if (dragging) {
                    int dx = e.motion.x - lastMouseX;
                    int dy = e.motion.y - lastMouseY;
                    pan_offset_x -= dx / size_offset;
                    pan_offset_y -= dy / size_offset;
                    lastMouseX = e.motion.x;
                    lastMouseY = e.motion.y;
                }
            }

            // Handle mouse button up events
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.button == SDL_BUTTON_RIGHT) {
                    dragging = false;
                    popup = false;
                }
            }

            // Handle mouse wheel events for zooming
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                // Zoom in or out based on the wheel movement
                if (e.wheel.y > 0) {
                    size_offset += 0.1 * size_offset; // Zoom in
                } else if (e.wheel.y < 0) {
                    size_offset -= 0.1 * size_offset; // Zoom out
                }
                // Clamp size_offset
                size_offset = std::max(size_offset, 0.1f);

                float afterzoom_screenX, afterzoom_screenY;
                SDL_GetMouseState(&afterzoom_screenX, &afterzoom_screenY);
                float afterzoom_worldX, afterzoom_worldY;
                afterzoom_worldX = static_cast<float>((afterzoom_screenX / size_offset) + pan_offset_x);
                afterzoom_worldY = static_cast<float>((afterzoom_screenY / size_offset) + pan_offset_y);

                pan_offset_x += (mouse_worldX - afterzoom_worldX);
                pan_offset_y += (mouse_worldY - afterzoom_worldY);
            }

            // Handle viewport resizing
            if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                current_window_width = e.window.data1;
                current_window_height = e.window.data2;
                viewport_output.w = current_window_width;
                viewport_output.h = current_window_height;
                printf("Window resized to %d x %d\n", current_window_width, current_window_height);
            }
        }

        // IMGUI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        {
            ImGui::Begin("Biome palette");

            static char buffer[32] = "";
            static bool upper = false;
            static bool layer_type = false;

            static char world_width_buffer[6] = "600";
            static char world_height_buffer[6] = "300";
            static char chunk_width_buffer[6] = "20";
            static char chunk_height_buffer[6] = "20";

            if(!world.HasInitializedCheck()){
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "World creation");

                ImGui::TextColored(error_color, "World has not been created/loaded, \nfill out parameters and create world \nor choose a world to load from the list.");

                ImGui::InputText("world width", world_width_buffer, sizeof(world_width_buffer));
                ImGui::InputText("world height", world_height_buffer, sizeof(world_height_buffer));
                ImGui::InputText("chunk width", chunk_width_buffer, sizeof(chunk_width_buffer));
                ImGui::InputText("chunk height", chunk_height_buffer, sizeof(chunk_height_buffer));

                ImGui::Text("Will result in lower world size of %d %d", atoi(world_width_buffer)*atoi(chunk_width_buffer), atoi(world_height_buffer)*atoi(chunk_height_buffer));
                if(atoi(world_width_buffer)*atoi(chunk_width_buffer)>=16384 || atoi(world_height_buffer)*atoi(chunk_height_buffer)>=16384){
                    ImGui::TextColored(warning_color, "Warning! Max texture(layer) size is 16384, \nexpect crashes or buggy behaviour.");
                    ImGui::TextColored(info_color, "Note: Expected to implement bigger worlds later.");
                }

                if(ImGui::Button("Create world")){
                    world.set_world_size(atoi(world_width_buffer), atoi(world_height_buffer));
                    world.set_chunk_size(atoi(chunk_width_buffer), atoi(chunk_height_buffer));
                    auto [world_width_lower_intermitent, world_height_lower_intermitent] = world.get_world_size(false);
                    auto [world_width_upper_intermitent, world_height_upper_intermitent] = world.get_world_size(true);
                    auto [chunk_width_intermitent, chunk_height_intermitent] = world.get_chunk_size();
                    world_width_lower = world_width_lower_intermitent;
                    world_height_lower = world_height_lower_intermitent;
                    world_width_upper = world_width_upper_intermitent;
                    world_height_upper = world_height_upper_intermitent;
                    chunk_width = chunk_width_intermitent;
                    chunk_height = chunk_height_intermitent;
                    texture_rect.w = world_width_lower;
                    texture_rect.h = world_height_lower;
                }

                ImGui::Separator();
            }

            if(world.HasInitializedCheck()){
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "World editing");

                ImGui::InputText("layer name##01",buffer,sizeof(buffer));

                if(ImGui::Button("is upper?##02")){
                    upper = !upper;
                }
                ImGui::SameLine();
                ImGui::Text(upper ? "Yes" : "No");

                if(ImGui::Button("layer type##02")){
                    layer_type = !layer_type;
                }
                ImGui::SameLine();
                ImGui::Text(layer_type ? "Icon" : "World");

                if(ImGui::Button("create layer##03")){
                    if(layer_type){
                        world.create_iconlayer(buffer);
                    } else {
                        world.create_worldlayer(renderer,buffer,upper);
                    }
                }

                if (ImGui::TreeNode("Layers")) {
                    if (ImGui::TreeNode("Icon Layers")) {
                        const auto& iconLayers = world.GetIconLayers();
                        int total = static_cast<int>(iconLayers.size());

                        for (auto it = iconLayers.rbegin(); it != iconLayers.rend(); ++it) {
                            int reverse_index = std::distance(iconLayers.rbegin(), it);
                            int actual_index = total - 1 - reverse_index;

                            std::string layer_name = it->layer_name;

                            if (ImGui::Button(layer_name.c_str())) {
                                selected_layer = layer_name;
                                std::cout << "Debug::SelectedLayer::IconLayer::" << layer_name << std::endl;
                            }

                            ImGui::SameLine();
                            if (ImGui::Button(("toggle##" + layer_name).c_str())) {
                                world.toggle_visibility_layer(layer_name);
                                std::cout << "Debug::ToggledVisibility::IconLayer::" << layer_name << std::endl;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("+##" + layer_name).c_str())) {
                                world.MoveIconLayer(actual_index, false);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("-##" + layer_name).c_str())) {
                                world.MoveIconLayer(actual_index, true);
                            }
                            ImGui::SameLine();
                            ImGui::Text(it->visible ? "Visible" : "Hidden");
                        }

                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNode("World Layers")) {
                        const auto& layers = world.GetWorldLayers();
                        int total = static_cast<int>(layers.size());

                        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
                            int reverse_index = std::distance(layers.rbegin(), it);
                            int actual_index = total - 1 - reverse_index;

                            std::string layer_name = it->layer_name;

                            if (ImGui::Button(layer_name.c_str())) {
                                selected_layer = layer_name;
                                std::cout << "Debug::SelectedLayer::WorldLayer::" << layer_name << std::endl;
                            }

                            ImGui::SameLine();
                            if (ImGui::Button(("toggle##" + layer_name).c_str())) {
                                world.toggle_visibility_layer(layer_name);
                                std::cout << "Debug::ToggledVisibility::WorldLayer::" << layer_name << std::endl;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("+##" + layer_name).c_str())) {
                                world.MoveWorldLayer(actual_index, false);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("-##" + layer_name).c_str())) {
                                world.MoveWorldLayer(actual_index, true);
                            }
                            ImGui::SameLine();
                            ImGui::Text(it->visible ? "Visible" : "Hidden");
                        }

                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }
                ImGui::Separator();
            }

            if(world.selected_world_icon){
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Icon parameters");

                ImGui::Text("Icon id: %d", world.selected_world_icon->GetIconId());

                if(ImGui::Button("Wipe decorators")){
                    world.selected_world_icon->clear_decorators();
                }

                ImGui::Separator();
            }

            ImGui::End();
        }
 
        if (popup)
        {
            ImGui::OpenPopup("dropdownmenu");
        }
        if (ImGui::BeginPopup("dropdownmenu"))
        {
            ImGui::Text("popup");
            ImGui::Separator();

            // if (ImGui::TreeNode("Biome ID selection")){
            //     for (int i = 0; i <= 255; ++i) {
            //         auto it = world.TILE_ID_MAP.find(i);
            //         if (it == world.TILE_ID_MAP.end()) {
            //             break;
            //         } else {
            //             auto [r, g, b] = it->second;
            //             ImVec4 selection_color = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
            //             if (ImGui::ColorButton(("##" + std::to_string(i)).c_str(), selection_color, 0, ImVec2(32, 16))){
            //                 printf("Debug::SetID::%d\n", i);
            //                 selected_tile_id = i;
            //             }
            //             ImGui::SameLine(); ImGui::Text("ID:%d", i);

            //             if(i%2!=0){
            //                 ImGui::SameLine();
            //             }
            //         }
            //     }

            //     ImGui::TreePop();
            // }

            for(auto& id_map : world.IDmaps){
                std::string label = "IDs: " + id_map.name;
                if (ImGui::TreeNode(label.c_str())) {
                    for (int i = 0; i <= 255; ++i) {
                        auto it = id_map.id_map.find(i);
                        if (it == id_map.id_map.end()) {
                            break;
                        } else {
                            auto [r, g, b] = it->second;
                            ImVec4 selection_color = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                            if (ImGui::ColorButton(("##" + std::to_string(i)).c_str(), selection_color, 0, ImVec2(32, 16))){
                                printf("Debug::SetID::%d\n", i);
                                selected_tile_id = i;
                                selected_idmap = id_map.name;
                            }
                            ImGui::SameLine(); ImGui::Text("ID:%d", i);
                        }
                    }

                    ImGui::TreePop();
                }
            }

            if (ImGui::TreeNode("Civilian Icon selection")){
                for (int i = 1; i <= 255; ++i) {
                    std::string filename = world.CivilianIdMap[i];
                    std::string filename_string = "icons/civilian/" + std::to_string(i) + "_" + filename + ".png";
                    const char* char_buffer = filename_string.c_str();
                    SDL_Surface* img_surface = IMG_Load(char_buffer);
                    if (!img_surface) {
                        // std::cerr << "IMG_Load failed for " << filename_string << ": " << SDL_GetError() << "<>" << i << std::endl;
                        break;
                    }else{
                        SDL_Texture* tex_buffer = SDL_CreateTextureFromSurface(renderer, img_surface);
                        if (!tex_buffer) {
                            std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
                            SDL_DestroySurface(img_surface);  // always free surface if texture creation fails
                        }

                        ImVec2 icon_size = {32, 32};
                        ImTextureID icon_texture_ref;
                        if(ImGui::ImageButton(("##" + std::to_string(i)).c_str(), (ImTextureID)(intptr_t)tex_buffer, icon_size)){
                            selected_icon_id = i;
                            selected_icon_class = 1;
                        }

                        ImGui::SameLine(); ImGui::Text("ID:%d", i);
                    }
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Military Icon selection")){
                for (int i = 1; i <= 255; ++i) {
                    std::string filename = world.MilitaryIdMap[i];
                    std::string filename_string = "icons/military/" + std::to_string(i) + "_" + filename + ".png";
                    const char* char_buffer = filename_string.c_str();
                    SDL_Surface* img_surface = IMG_Load(char_buffer);
                    if (!img_surface) {
                        // std::cerr << "IMG_Load failed for " << filename_string << ": " << SDL_GetError() << "<>" << i << std::endl;
                        break;
                    }else{
                        SDL_Texture* tex_buffer = SDL_CreateTextureFromSurface(renderer, img_surface);
                        if (!tex_buffer) {
                            std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
                            SDL_DestroySurface(img_surface);  // always free surface if texture creation fails
                        }

                        ImVec2 icon_size = {32, 32};
                        ImTextureID icon_texture_ref;
                        if(ImGui::ImageButton(("##" + std::to_string(i)).c_str(), (ImTextureID)(intptr_t)tex_buffer, icon_size)){
                            selected_icon_id = i;
                            selected_icon_class = 2;
                        }

                        ImGui::SameLine(); ImGui::Text("ID:%d", i);
                    }
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Decorator Icon selection")){
                for (int i = 1; i <= 255; ++i) {
                    std::string filename = world.DecoratorIdMap[i];
                    std::string filename_string = "icons/decorator/" + std::to_string(i) + "_" + filename + ".png";
                    const char* char_buffer = filename_string.c_str();
                    SDL_Surface* img_surface = IMG_Load(char_buffer);
                    if (!img_surface) {
                        // std::cerr << "IMG_Load failed for " << filename_string << ": " << SDL_GetError() << "<>" << i << std::endl;
                        break;
                    }else{
                        SDL_Texture* tex_buffer = SDL_CreateTextureFromSurface(renderer, img_surface);
                        if (!tex_buffer) {
                            std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
                            SDL_DestroySurface(img_surface);  // always free surface if texture creation fails
                        }

                        ImVec2 icon_size = {32, 32};
                        ImTextureID icon_texture_ref;
                        if(ImGui::ImageButton(("##" + std::to_string(i)).c_str(), (ImTextureID)(intptr_t)tex_buffer, icon_size)){
                            selected_decorator_id = i;

                            if(world.selected_world_icon){
                                world.selected_world_icon->add_decorator(renderer, i, world.DecoratorIdMap);
                            }
                        }

                        ImGui::SameLine(); ImGui::Text("ID:%d", i);
                    }
                }

                if(ImGui::Button("xx##decorator_deselect", ImVec2(32, 32))){
                    selected_decorator_id = 0;
                }
                ImGui::SameLine(); ImGui::Text("Deselect");

                ImGui::TreePop();
            }

            ImGui::Text("Controls");
            ImGui::Separator();

            if (ImGui::Button("move icon")) {
                moving_icon = !moving_icon;
                std::cout << "Debug::ToggledMovingIcon" << std::endl;
            }
            ImGui::SameLine();
            ImGui::Text(moving_icon ? "moving" : "not moving");

            if (ImGui::Button("rotate icon")) {
                rotating_icon = !rotating_icon;
                std::cout << "Debug::ToggledRotatingIcon" << std::endl;
            }
            ImGui::SameLine();
            ImGui::Text(rotating_icon ? "rotating" : "not rotating");

            if (ImGui::Button("edit map")) {
                editing_map = !editing_map;
                std::cout << "Debug::ToggledEditingMode" << std::endl;
            }
            ImGui::SameLine();
            ImGui::Text(editing_map ? "editing" : "interacting");

            ImGui::Separator();
            ImGui::Text("Line tools");

            if(ImGui::Button("Add p")){
                selected_linetool = "add";
            }
            if(ImGui::Button("Rmv p")){
                selected_linetool = "remove";
            }
            if(ImGui::Button("Non p")){
                selected_linetool = "";
            }
            
            ImGui::EndPopup();
        }

        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

        // clear the renderer
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        viewport_source_lower.x = pan_offset_x;
        viewport_source_lower.y = pan_offset_y;
        viewport_source_lower.w = current_window_width / size_offset;
        viewport_source_lower.h = current_window_height / size_offset;

        viewport_source_upper.x = pan_offset_x/chunk_width;
        viewport_source_upper.y = pan_offset_y/chunk_height;
        viewport_source_upper.w = current_window_width/chunk_width / size_offset;
        viewport_source_upper.h = current_window_height/chunk_height / size_offset;

        // Calculate intersection of viewport_source and texture bounds
        intersect.x = fmaxf(viewport_source_lower.x, texture_rect.x);
        intersect.y = fmaxf(viewport_source_lower.y, texture_rect.y);
        intersect.w = fminf(viewport_source_lower.x + viewport_source_lower.w, texture_rect.x + texture_rect.w) - intersect.x;
        intersect.h = fminf(viewport_source_lower.y + viewport_source_lower.h, texture_rect.y + texture_rect.h) - intersect.y;

        // Draw background color for out-of-bounds area
        SDL_SetRenderDrawColor(renderer, 5, 5, 5, 255);

        // Only render texture if intersection is valid
        if (intersect.w > 0 && intersect.h > 0) {
            viewport_output_bounded.x = (intersect.x - viewport_source_lower.x) * size_offset;
            viewport_output_bounded.y = (intersect.y - viewport_source_lower.y) * size_offset;
            viewport_output_bounded.w = intersect.w * size_offset;
            viewport_output_bounded.h = intersect.h * size_offset;

            if(world.HasInitializedCheck()){
                SDL_RenderFillRect(renderer, &viewport_output_bounded);
                
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                world.draw_all(renderer, &viewport_source_lower, &viewport_source_upper, &viewport_output_bounded, size_offset, pan_offset_x, pan_offset_y);
            }
        }

        // IMGUI
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        // ---

        SDL_RenderPresent(renderer);
    }

    // IMGUI
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    // ---

    // Cleanup
    SDL_Quit();

    return 0;
}
