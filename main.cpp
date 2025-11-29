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
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <queue>

// --- CONFIG ---

bool ENABLE_TIPS = true;
bool ENABLE_DEBUG = true;

// function to find all savefiles in the current directory
std::vector<std::string> find_savefiles(const std::string& directory) {
    std::vector<std::string> savefiles;
    std::regex pattern(R"((.+)\.nw$)"); // *.nw

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        if (std::regex_match(filename, pattern)) {
            savefiles.push_back(filename);
        }
    }

    return savefiles;
}

std::vector<std::string> find_savefiles_internet() {
    std::vector<std::string> savefiles;

    std::ifstream file("AWSshared");
    if (!file.is_open()) {
        std::cerr << "Could not open AWSshared\n";
        return savefiles;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty())
            savefiles.push_back(line);
    }

    return savefiles;
}

struct IDmap {
    std::unordered_map<int, std::tuple<int, int, int, std::string>> id_map;
    std::vector<uint8_t> id_LUT; 
    Uint32 px_LUT[256]; // id_map as a lookup table
    std::string name;

    void buildFastLUT() {
        id_LUT.assign(1 << 24, 0xFF); // 0xFF = "not mapped"
        for (auto& [id, rgb] : id_map) {
            auto [r, g, b, text] = rgb;
            uint32_t key = (r << 16) | (g << 8) | b;
            id_LUT[key] = static_cast<uint8_t>(id);
        }

        SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA8888;
        const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(format);
        for (int i = 0; i < 256; i++) {
            auto it = id_map.find(i);
            if (it != id_map.end()) {
                Uint8 r, g, b;
                std::string text;
                std::tie(r, g, b, text) = it->second;
                if(i==255){
                    px_LUT[i] = SDL_MapRGBA(fmt, nullptr, 0, 0, 0, 0); // index 255 is transparent
                } else {
                    px_LUT[i] = SDL_MapRGBA(fmt, nullptr, r, g, b, 255);
                }
            }
        }
    }
};

const ImVec4 info_color = {0.4f, 0.6f, 1.0f, 1.0f};
const ImVec4 warning_color = {1.0f, 0.8f, 0.3f, 1.0f};
const ImVec4 error_color = {1.0f, 0.3f, 0.3f, 1.0f};

const int SCREEN_WIDTH = 1280; // Width of the window (default)
const int SCREEN_HEIGHT = 720; // Height of the window (default)

int current_window_width = SCREEN_WIDTH; // Current width of the window, used for rendering
int current_window_height = SCREEN_HEIGHT; // Current height of the window, used for rendering

static bool dragging = false; // Flag to indicate if the user is dragging the mouse
static float lastMouseX = 0.0f, lastMouseY = 0.0f; // Last mouse coordinates before dragging starts 

inline void ClampRectToTexture(SDL_Rect& r, int textureWidth, int textureHeight) {
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }

    if (r.x + r.w > textureWidth)
        r.w = textureWidth - r.x;

    if (r.y + r.h > textureHeight)
        r.h = textureHeight - r.y;

    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
}

inline Uint32 GetPixel(Uint32* pixels, int pitch, int x, int y) {
    return pixels[y * (pitch / 4) + x];
}

inline void SetPixelGlobal(Uint32* pixels, int pitch, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    Uint32 color = (Uint32(r) << 24) | (Uint32(g) << 16) | (Uint32(b) << 8) | Uint32(a);
    pixels[y * (pitch / 4) + x] = color;
}

void PaintBrush(Uint32* pixels, int pitch, int radius, Uint32 color)
{
    int rowPixels = pitch / 4; 

    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            if (dx*dx + dy*dy <= radius * radius)
            {
                int px = dx + radius;
                int py = dy + radius;

                pixels[py * rowPixels + px] = color;
            }
        }
    }
}

void PaintFill(Uint32* pixels, int pitch, int radius, Uint32 target_color, Uint32 color)
{
    int rowPixels = pitch / 4;

    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            if (dx*dx + dy*dy <= radius * radius)
            {
                int px = dx + radius;
                int py = dy + radius;
                int idx = py * rowPixels + px;

                if (pixels[idx] == target_color) {
                    pixels[idx] = color;
                }
            }
        }
    }
}

inline void SetPixelLocal(Uint32* pixels, int pitch, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
    Uint32 color = (Uint32(r) << 24) | (Uint32(g) << 16) | (Uint32(b) << 8) | Uint32(a);
    pixels[0] = color;
}

double distanceSquared(double x1, double y1, double x2, double y2) {
    return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
}

struct IconBase {
    virtual ~IconBase() = default;

    virtual SDL_FPoint GetPosition() const = 0;
    virtual int GetIconId() const = 0;
    virtual std::tuple<float, float> GetSize() const = 0;

    virtual void SetPosition(float x_, float y_) = 0;
    virtual void SetIconId(int id) = 0;

    virtual void SetSelectionStatus(bool input_boolean) = 0;

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
    bool selected = false;

    SDL_FRect viewport_local;

    std::tuple<float, float> GetSize() const override {
        std::tuple<float, float> output = {width*scale, height*scale};
        return output;
    }

    SDL_FPoint GetPosition() const override { return position; }

    int GetIconId() const override { return icon_id; }

    void SetPosition(float x_, float y_) {
        position.x = x_;
        position.y = y_;
    } 

    void SetSelectionStatus(bool input_boolean) { selected = input_boolean; }

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
            std::cout<<"Icon id is undefined, no texture will be assigned: "<<icon_id<<std::endl;
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

        if (selected) {
            SDL_FRect selection_rectangle = {
                viewport_local.x-viewport_local.w,
                viewport_local.y-viewport_local.h,
                viewport_local.w*3.0f,
                viewport_local.h*3.0f
            };
            SDL_RenderRect(renderer, &selection_rectangle);
        }
    }
};

struct IconMilitary : public IconBase {
    SDL_Texture* icon_texture;
    SDL_Texture* decorator_texture;
    SDL_FPoint position;
    SDL_FPoint center;
    float width, height, scale=1.0f, angle=0.0;
    int icon_id=0, country_id=0, quality=0;
    bool selected = false;
    std::vector<IconDecorator> decorators;

    SDL_FRect viewport_local;
    SDL_FRect viewport_local_decorator;

    std::tuple<float, float> GetSize() const override {
        std::tuple<float, float> output = {width*scale, height*scale};
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

    void SetSelectionStatus(bool input_boolean) { selected = input_boolean; }

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
            std::cout<<"Icon id is undefined, no texture will be assigned: "<<icon_id<<std::endl;
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
            viewport_local_decorator.y = (position_y_scaled - pan_y_scaled - height_scaled / 2.0f)-(height_scaled*index)/2;
            viewport_local_decorator.w = width_scaled;
            viewport_local_decorator.h = height_scaled;

            SDL_RenderTexture(renderer, decorator_icon.decorator_texture, NULL, &viewport_local_decorator);
            index++;
        }

        if (selected) {
            SDL_FRect selection_rectangle = {
                viewport_local.x-viewport_local.w,
                viewport_local.y-viewport_local.h,
                viewport_local.w*3.0f,
                viewport_local.h*3.0f
            };
            SDL_RenderRect(renderer, &selection_rectangle);
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

    void RemovePointAtIndex(int index) {
        if (index < 0 || index >= size) return;

        for (int i = index; i < size - 1; ++i) {
            point_array[i] = point_array[i + 1];
        }
        size--;
    }

    void QueryRemovePoint(float x, float y, float threshold = 1.0) {
        float thresholdSq = threshold * threshold; 
        for (int i = 0; i < size; ++i) {
            float dx = point_array[i].x - x;
            float dy = point_array[i].y - y;
            float distSq = dx * dx + dy * dy;

            if (distSq <= thresholdSq) {
                RemovePointAtIndex(i);
                return;
            }
        }
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

    void remove_icon(IconBase* icon){
        auto it_civilian = std::find_if(IconsCivilian.begin(), IconsCivilian.end(),
            [icon](const IconCivilian& ic) { return &ic == icon; });
        if (it_civilian != IconsCivilian.end()) {
            IconsCivilian.erase(it_civilian);
            return;
        }

        auto it_military = std::find_if(IconsMilitary.begin(), IconsMilitary.end(),
            [icon](const IconMilitary& im) { return &im == icon; });
        if (it_military != IconsMilitary.end()) {
            IconsMilitary.erase(it_military);
            return;
        }
    }

    void create_shape(Shape shape, Uint8 r = 255, Uint8 g = 255, Uint8 b = 255, Uint8 a = 255){
        if(shape.GetSize()>=2){
            shape.r = r;
            shape.g = g;
            shape.b = b;
            shape.a = a;
            Shapes.push_back(shape);
        } else {
            std::cout<<"Debug::Shape::PushBack::Error"<<std::endl;
        }
    }
};

struct WorldLayer{
    std::string layer_name;
    std::string idmap_name;
    bool visible = true;
    bool is_upper;

    SDL_Texture* layer_texture;
};

struct PoliticalLayer{
    std::string layer_name;
    std::string idmap_name;
    WorldLayer* world_layer;
    bool visible = true;

    //! Political layer will always be upper
    //? This is an intentional design decision

    SDL_Texture* layer_texture;
    SDL_Texture* shadow_texture;

    void update_texture(std::vector<IDmap> IDmaps){
        if(!world_layer) return;

        IDmap* referenced_id_map = nullptr;
        for (auto& id_map : IDmaps) {
            if (id_map.name == world_layer->idmap_name) {
                referenced_id_map = &id_map;
                break;
            }
        }

        void* shadow_pixels;
        int shadow_pitch;
        void* world_pixels;
        int world_pitch;

        if (SDL_LockTexture(shadow_texture, nullptr, (void**)&shadow_pixels, &shadow_pitch) < 0) {
            std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
            return;
        }
        if (SDL_LockTexture(world_layer->layer_texture, nullptr, (void**)&world_pixels, &world_pitch) < 0) {
            std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
            return;
        }

        for(int i=0; i<world_layer->layer_texture->h; i++){
            for(int j=0; j<world_layer->layer_texture->w; j++){
                Uint32* world_px = (Uint32*)((Uint8*)world_pixels + i * world_pitch) + j;
                Uint8 r = (*world_px >> 24) & 0xFF;
                Uint8 g = (*world_px >> 16) & 0xFF;
                Uint8 b = (*world_px >> 8)  & 0xFF;
                uint32_t key = (r << 16) | (g << 8) | b;
                uint8_t index = referenced_id_map->id_LUT[key];
                if(index == 0 || index == 0xFF){
                    SetPixelGlobal((Uint32*)shadow_pixels, shadow_pitch, j, i, 0, 0, 0, 100);
                } else {
                    SetPixelGlobal((Uint32*)shadow_pixels, shadow_pitch, j, i, 0, 0, 0, 0);
                }
            }
        }

        SDL_UnlockTexture(shadow_texture);
        SDL_UnlockTexture(layer_texture);
        SDL_UnlockTexture(world_layer->layer_texture);
    }

    void bake_texture(std::vector<IDmap> IDmaps){
        void* layer_pixels;
        int layer_pitch;

        if (SDL_LockTexture(layer_texture, nullptr, (void**)&layer_pixels, &layer_pitch) < 0) {
            std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
            return;
        }
        
        for(int i=0; i<world_layer->layer_texture->h; i++){
            for(int j=0; j<world_layer->layer_texture->w; j++){
                SetPixelGlobal((Uint32*)layer_pixels, layer_pitch, j, i, 30, 30, 30, 255);
            }
        }

        SDL_UnlockTexture(layer_texture);
    }
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
        std::vector<PoliticalLayer> PoliticalLayers;
        std::string last_created_layer_name = "name";

    public:
        bool WORLD_HAS_INITIALIZED = false;

        int UPPER_WORLD_WIDTH=0; // number of chunks along the X-axis, i.e bigger sections of tiles
        int UPPER_WORLD_HEIGHT=0; // number of chunks along the Y-axis, i.e bigger sections of tiles
        int CHUNK_WIDTH=0; // Number of tiles in a chunk along the X-axis
        int CHUNK_HEIGHT=0; // Number of tiles in a chunk along the Y-axis
        int LOWER_WORLD_WIDTH=0; // Total number of tiles along the X-axis
        int LOWER_WORLD_HEIGHT=0; // Total number of tiles along the Y-axis

        std::vector<IDmap> IDmaps;
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
            if(get_layer_type(name_id)==1){
                WorldLayer& it = get_worldlayer(name_id);
                it.visible = !it.visible;
                std::cout<<"Debug::WorldLayer::Visibility::"<<it.visible<<std::endl;
            }
            if(get_layer_type(name_id)==2){
                IconLayer& it = get_iconlayer(name_id);
                it.visible = !it.visible;
                std::cout<<"Debug::IconLayer::Visibility::"<<it.visible<<std::endl;
            }
            if(get_layer_type(name_id)==3){
                PoliticalLayer& it = get_politicallayer(name_id);
                it.visible = !it.visible;
                std::cout<<"Debug::PoliticalLayer::Visibility::"<<it.visible<<std::endl;
            }
        }

        std::vector<IconLayer>& GetIconLayers() { return IconLayers; }

        std::vector<WorldLayer>& GetWorldLayers() { return WorldLayers; }

        std::vector<PoliticalLayer>& GetPoliticalLayers() { return PoliticalLayers; }

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

        void MovePoliticalLayer(size_t index, bool down) {
            if(down){
                if (index == 0 || index >= PoliticalLayers.size()) return;
                std::swap(PoliticalLayers[index], PoliticalLayers[index - 1]);
            } else {
                if (index >= PoliticalLayers.size() - 1) return;
                std::swap(PoliticalLayers[index], PoliticalLayers[index + 1]);
            }
        }

        void SaveWorld(std::string filename = "savename.nw", bool cloud = false) {
            std::cout << "Debug::SaveWorld::" << filename << std::endl;
            std::string full_filename = "saves/" + filename;

            std::ofstream out(full_filename, std::ios::binary);
            if (!out) {
                std::cerr << "Failed to open save file\n";
                return;
            }

            // Write world dimensions
            int32_t world_width  = UPPER_WORLD_WIDTH;
            int32_t world_height = UPPER_WORLD_HEIGHT;
            int32_t chunk_width  = CHUNK_WIDTH;
            int32_t chunk_height = CHUNK_HEIGHT;
            out.write(reinterpret_cast<char*>(&world_width), sizeof(world_width));
            out.write(reinterpret_cast<char*>(&world_height), sizeof(world_height));
            out.write(reinterpret_cast<char*>(&chunk_width), sizeof(chunk_width));
            out.write(reinterpret_cast<char*>(&chunk_height), sizeof(chunk_height));

            // Number of layers
            int32_t num_layers_world = WorldLayers.size();
            out.write(reinterpret_cast<char*>(&num_layers_world), sizeof(num_layers_world));

            int32_t num_layers_political = PoliticalLayers.size();
            out.write(reinterpret_cast<char*>(&num_layers_political), sizeof(num_layers_political));

            int32_t num_layers_icon = IconLayers.size();
            out.write(reinterpret_cast<char*>(&num_layers_icon), sizeof(num_layers_icon));

            // World layers
            for (auto& world_layer : WorldLayers) {
                // Find referenced IDmap
                IDmap* referenced_id_map = nullptr;
                for (auto& id_map : IDmaps) {
                    if (id_map.name == world_layer.idmap_name) {
                        referenced_id_map = &id_map;
                        break;
                    }
                }
                if (!referenced_id_map) {
                    std::cerr << "No IDmap found for layer " << world_layer.layer_name << "\n";
                    continue;
                }

                // Query texture
                SDL_PixelFormat format = world_layer.layer_texture->format;
                int width = world_layer.layer_texture->w; 
                int height = world_layer.layer_texture->h;
                const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(format);

                // Lock texture
                void* pixels;
                int pitch;
                if (SDL_LockTexture(world_layer.layer_texture, nullptr, &pixels, &pitch) < 0) {
                    std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                    continue;
                }

                // Metadata / header stuff
                int32_t lnameLen = world_layer.layer_name.size();
                out.write(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                out.write(world_layer.layer_name.data(), lnameLen);

                int32_t idnameLen = world_layer.idmap_name.size();
                out.write(reinterpret_cast<char*>(&idnameLen), sizeof(idnameLen));
                out.write(world_layer.idmap_name.data(), idnameLen);

                int32_t w = width, h = height;
                out.write(reinterpret_cast<char*>(&w), sizeof(w));
                out.write(reinterpret_cast<char*>(&h), sizeof(h));

                uint8_t isUpper = world_layer.is_upper ? 1 : 0;
                out.write(reinterpret_cast<char*>(&isUpper), sizeof(isUpper));

                // Streaming pixel to file
                std::vector<uint8_t> rowBuffer(width);

                uint8_t* row = static_cast<uint8_t*>(pixels);
                for (int y = 0; y < height; ++y) {
                    Uint32* px = reinterpret_cast<Uint32*>(row);
                    for (int x = 0; x < width; ++x) {
                        Uint32 pixel = px[x];

                        Uint8 r = (pixel >> 24) & 0xFF;
                        Uint8 g = (pixel >> 16) & 0xFF;
                        Uint8 b = (pixel >> 8)  & 0xFF;

                        uint32_t key = (r << 16) | (g << 8) | b;
                        uint8_t index = referenced_id_map->id_LUT[key];
                        if (index == 0xFF) index = 255;

                        rowBuffer[x] = index;
                    }
                    out.write(reinterpret_cast<char*>(rowBuffer.data()), width); // flush row
                    row += pitch;
                }

                SDL_UnlockTexture(world_layer.layer_texture);
                std::cout << "Debug::LayerSaved::" << world_layer.layer_name << std::endl;
            }

            // Political layers
            for (auto& political_layer : PoliticalLayers) {
                // Find referenced IDmap
                IDmap* referenced_id_map = nullptr;
                for (auto& id_map : IDmaps) {
                    if (id_map.name == political_layer.idmap_name) {
                        referenced_id_map = &id_map;
                        break;
                    }
                }
                if (!referenced_id_map) {
                    std::cerr << "No IDmap found for layer " << political_layer.layer_name << "\n";
                    continue;
                }

                // Query texture
                SDL_PixelFormat format = political_layer.layer_texture->format;
                int width = political_layer.layer_texture->w; 
                int height = political_layer.layer_texture->h;
                const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(format);

                // Lock texture
                void* pixels;
                int pitch;
                if (SDL_LockTexture(political_layer.layer_texture, nullptr, &pixels, &pitch) < 0) {
                    std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                    continue;
                }

                // Metadata / header stuff
                int32_t lnameLen = political_layer.layer_name.size();
                out.write(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                out.write(political_layer.layer_name.data(), lnameLen);

                int32_t idnameLen = political_layer.idmap_name.size();
                out.write(reinterpret_cast<char*>(&idnameLen), sizeof(idnameLen));
                out.write(political_layer.idmap_name.data(), idnameLen);

                int32_t world_layer_name_len = political_layer.world_layer->layer_name.size();
                out.write(reinterpret_cast<char*>(&world_layer_name_len), sizeof(world_layer_name_len));
                out.write(political_layer.world_layer->layer_name.data(), world_layer_name_len);

                int32_t w = width, h = height;
                out.write(reinterpret_cast<char*>(&w), sizeof(w));
                out.write(reinterpret_cast<char*>(&h), sizeof(h));

                // Streaming pixel to file
                std::vector<uint8_t> rowBuffer(width);

                uint8_t* row = static_cast<uint8_t*>(pixels);
                for (int y = 0; y < height; ++y) {
                    Uint32* px = reinterpret_cast<Uint32*>(row);
                    for (int x = 0; x < width; ++x) {
                        Uint32 pixel = px[x];

                        Uint8 r = (pixel >> 24) & 0xFF;
                        Uint8 g = (pixel >> 16) & 0xFF;
                        Uint8 b = (pixel >> 8)  & 0xFF;

                        uint32_t key = (r << 16) | (g << 8) | b;
                        uint8_t index = referenced_id_map->id_LUT[key];

                        rowBuffer[x] = index;
                    }
                    out.write(reinterpret_cast<char*>(rowBuffer.data()), width); // flush row
                    row += pitch;
                }

                SDL_UnlockTexture(political_layer.layer_texture);
                std::cout << "Debug::LayerSaved::" << political_layer.layer_name << std::endl;
            }

            // Icon layers
            for (auto& icon_layer : IconLayers) {
                // Layer name
                int32_t lnameLen = icon_layer.layer_name.size();
                out.write(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                out.write(icon_layer.layer_name.data(), lnameLen);

                // Civilian icons
                int32_t num_civilian_icons = icon_layer.IconsCivilian.size();
                out.write(reinterpret_cast<char*>(&num_civilian_icons), sizeof(num_civilian_icons));

                for (auto& icon : icon_layer.IconsCivilian) {
                    out.write(reinterpret_cast<char*>(&icon.icon_id), sizeof(icon.icon_id));
                    out.write(reinterpret_cast<char*>(&icon.position.x), sizeof(icon.position.x));
                    out.write(reinterpret_cast<char*>(&icon.position.y), sizeof(icon.position.y));
                }

                // Military icons
                int32_t num_military_icons = icon_layer.IconsMilitary.size();
                out.write(reinterpret_cast<char*>(&num_military_icons), sizeof(num_military_icons));

                for (auto& icon : icon_layer.IconsMilitary) {
                    out.write(reinterpret_cast<char*>(&icon.icon_id), sizeof(icon.icon_id));
                    out.write(reinterpret_cast<char*>(&icon.angle), sizeof(icon.angle));
                    out.write(reinterpret_cast<char*>(&icon.country_id), sizeof(icon.country_id));
                    out.write(reinterpret_cast<char*>(&icon.quality), sizeof(icon.quality));
                    out.write(reinterpret_cast<char*>(&icon.position.x), sizeof(icon.position.x));
                    out.write(reinterpret_cast<char*>(&icon.position.y), sizeof(icon.position.y));

                    int32_t num_decorators = icon.decorators.size();
                    out.write(reinterpret_cast<char*>(&num_decorators), sizeof(num_decorators));
                    for (auto& decorator : icon.decorators) {
                        out.write(reinterpret_cast<char*>(&decorator.id), sizeof(decorator.id));
                    }
                }

                // Shapes
                int32_t num_shapes = icon_layer.Shapes.size();
                out.write(reinterpret_cast<char*>(&num_shapes), sizeof(num_shapes));

                for (auto& shape : icon_layer.Shapes) {
                    out.write(reinterpret_cast<char*>(&shape.r), sizeof(shape.r));
                    out.write(reinterpret_cast<char*>(&shape.g), sizeof(shape.g));
                    out.write(reinterpret_cast<char*>(&shape.b), sizeof(shape.b));
                    out.write(reinterpret_cast<char*>(&shape.a), sizeof(shape.a));

                    int32_t num_points = shape.GetSize();
                    out.write(reinterpret_cast<char*>(&num_points), sizeof(num_points));

                    for (int i = 0; i < num_points; ++i) {
                        out.write(reinterpret_cast<char*>(&shape.point_array[i].x), sizeof(shape.point_array[i].x));
                        out.write(reinterpret_cast<char*>(&shape.point_array[i].y), sizeof(shape.point_array[i].y));
                    }
                }
            }

            std::cout << "Debug::World saved successfully to " << filename << std::endl;
            out.close();

            if(cloud) {
                std::string command = "AWSupload.exe " + filename;
                int AWSupload_exitcode = std::system(command.c_str());
                if (AWSupload_exitcode == 0) {
                    std::cout << "AWSupload completed successfully: " << AWSupload_exitcode << std::endl;
                } else {
                    std::cout << "AWSupload failed to upload: " << AWSupload_exitcode << std::endl;
                }
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
            for (const auto &entry : std::filesystem::directory_iterator("ids")) {
                if (!entry.is_regular_file())
                    continue;

                std::ifstream file(entry.path());
                if (!file.is_open()) {
                    std::cerr<<"Debug::OpenFile::Error::"<<entry.path().filename().string()<<std::endl;
                    continue;
                }

                std::string line;
                IDmap temporary_map;
                bool inSection = false;

                while (std::getline(file, line)) {
                    if (line.rfind("...", 0) == 0) {
                        if (line == "...;") {
                            if (inSection) {
                                temporary_map.buildFastLUT();
                                IDmaps.push_back(temporary_map);
                                temporary_map = IDmap();
                                inSection = false;
                            }
                        } else {
                            temporary_map = IDmap();
                            temporary_map.name = line.substr(3);
                            inSection = true;
                        }
                        continue;
                    }

                    if (!inSection)
                        continue;

                    std::istringstream iss(line);
                    int id, r, g, b;
                    std::string text;

                    if (!(iss >> id >> r >> g >> b)) {
                        std::cerr << "Debug::MalformedLine => " << line << std::endl;
                        continue;
                    }

                    std::getline(iss, text);
                    if (!text.empty() && text[0] == ' ')
                        text.erase(0, 1);

                    temporary_map.id_map[id] = std::make_tuple(r, g, b, text);
                }

                if (inSection) {
                    temporary_map.buildFastLUT();
                    IDmaps.push_back(temporary_map);
                }

                file.close();
            }
            for (auto& map : IDmaps) {
                std::cout << "ID Map: " << map.name << std::endl;
            }
        }

        WorldLayer& create_worldlayer(SDL_Renderer* renderer, const std::string& name_id, bool is_upper, std::string selected_idmap) {
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
            world_layer.idmap_name = selected_idmap;

            WorldLayers.push_back(world_layer);
            std::cout<<"Debug::Created::WorldLayer::"<<world_layer.layer_name<<std::endl;
            last_created_layer_name = world_layer.layer_name;
            return WorldLayers.back();
        }

        PoliticalLayer& create_politicallayer(SDL_Renderer* renderer, const std::string& name_id, std::string selected_idmap, WorldLayer& linked_world_layer){
            int width = UPPER_WORLD_WIDTH;
            int height = UPPER_WORLD_HEIGHT;

            if (width <= 0 || height <= 0) {
                std::cerr << "Invalid texture size: " << width << " x " << height << std::endl;
                throw std::runtime_error("Layer not found");
            }

            SDL_Texture *layer_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
            SDL_Texture *shadow_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);

            if (!layer_texture || !shadow_texture) {
                SDL_Log("Failed to create texture '%s': %s", name_id.c_str(), SDL_GetError());
                throw std::runtime_error("Layer not found");
            }

            SDL_SetTextureScaleMode(layer_texture, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureScaleMode(shadow_texture, SDL_SCALEMODE_NEAREST);

            PoliticalLayer political_layer;
            political_layer.world_layer = &linked_world_layer;
            if(name_id.length()!=0){
                political_layer.layer_name = name_id;
            } else {
                political_layer.layer_name = last_created_layer_name+"_a";
            }
            political_layer.layer_texture = layer_texture;
            political_layer.shadow_texture = shadow_texture;
            political_layer.idmap_name = selected_idmap;

            PoliticalLayers.push_back(political_layer);
            std::cout<<"Debug::Created::PoliticalLayer::"<<political_layer.layer_name<<std::endl;
            last_created_layer_name = political_layer.layer_name;
            return PoliticalLayers.back();
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

        PoliticalLayer& get_politicallayer(const std::string& name_id) {
            for (auto& layer : PoliticalLayers) {
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

        int get_layer_type(const std::string& name_id) {
            for (auto& layer : WorldLayers) {
                if (layer.layer_name == name_id) {
                    return 1;
                }
            }
            for (auto& layer : IconLayers) {
                if (layer.layer_name == name_id) {
                    return 2;
                }
            }
            for (auto& layer : PoliticalLayers) {
                if (layer.layer_name == name_id) {
                    return 3;
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
                SDL_Texture* texture = layer.layer_texture;

                if(layer.visible){
                    if(layer.is_upper){
                        SDL_RenderTexture(renderer, texture, input_viewport_upper, output_viewport);
                    } else {
                        SDL_RenderTexture(renderer, texture, input_viewport_lower, output_viewport);
                    }
                }
            };
            for (auto& layer : PoliticalLayers) {
                SDL_Texture* texture = layer.layer_texture;
                SDL_Texture* shadow_texture = layer.shadow_texture;

                if(layer.visible){
                    SDL_RenderTexture(renderer, texture, input_viewport_upper, output_viewport);
                    SDL_RenderTexture(renderer, shadow_texture, input_viewport_upper, output_viewport);
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
                        // if (icon.GetPosition().x == selected_world_icon->GetPosition().x && icon.GetPosition().y == selected_world_icon->GetPosition().y) {

                        // }
                    }
                    for (auto& icon : icon_layer.IconsMilitary) {
                        icon.render_to_view(renderer, output_viewport, scale_offset, pan_offset_x, pan_offset_y);
                    }
                };
            };
        };
    };

int main(int argc, char* args[]) {
    std::cout<<"Initializing AWS, it'll take a second..."<<std::endl;
    std::string command = "AWSlistbucket.exe";
    int AWSlistbucket_exitcode = std::system(command.c_str());
    if (AWSlistbucket_exitcode == 0) {
        std::cout << "AWSlistbucket completed successfully: " << AWSlistbucket_exitcode << std::endl;
    } else {
        std::cout << "AWSlistbucket failed with exit code: " << AWSlistbucket_exitcode << std::endl;
    }

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

    // IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowMinSize        = ImVec2( 160, 20 );
        style.FramePadding         = ImVec2( 4, 2 );
        style.ItemSpacing          = ImVec2( 6, 2 );
        style.ItemInnerSpacing     = ImVec2( 6, 4 );
        style.Alpha                = 0.95f;
        style.WindowRounding       = 4.0f;
        style.FrameRounding        = 2.0f;
        style.IndentSpacing        = 6.0f;
        style.ItemInnerSpacing     = ImVec2( 2, 4 );
        style.ColumnsMinSpacing    = 50.0f;
        style.GrabMinSize          = 14.0f;
        style.GrabRounding         = 16.0f;
        style.ScrollbarSize        = 12.0f;
        style.ScrollbarRounding    = 16.0f;

        style.Colors[ImGuiCol_Text]                  = ImVec4(0.86f, 0.93f, 0.89f, 0.75f);
        style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.86f, 0.93f, 0.89f, 0.25f);
        style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.20f, 0.20f, 0.20f, 0.25f);
        style.Colors[ImGuiCol_Border]                = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
        style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.40f, 0.40f, 0.40f, 0.75f);
        style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
        style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.20f, 0.22f, 0.27f, 0.45f);
        style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.92f, 0.18f, 0.29f, 0.75f);
        style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.47f, 0.77f, 0.83f, 0.15f);
        style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_Button]                = ImVec4(0.47f, 0.77f, 0.83f, 0.15f);
        style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
        style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_Header]                = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_Separator]             = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
        style.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.47f, 0.77f, 0.83f, 0.05f);
        style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.40f, 0.40f, 0.40f, 0.75f);
        style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.20f, 0.20f, 0.20f, 0.90f);
    }
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
    int brush_tool = 0;
    int selected_icon_id;
    int selected_icon_class;
    int selected_country_id = 0;
    int selected_tile_id = 0;
    float size_offset = 1.0f;
    float pan_offset_x = 0.0f;
    float pan_offset_y = 0.0f;
    bool popup = false;
    int brush_radius = 10;

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
            // ---

            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE)) {
                world.SaveWorld("quicksave.nw", false);
                quit = true;
            }

            // <float> mouse coordinates in screen space
            float mouse_screenX, mouse_screenY;
            SDL_GetMouseState(&mouse_screenX, &mouse_screenY);
            // <float> mouse coordinates in world space
            float mouse_worldX, mouse_worldY;
            mouse_worldX = static_cast<float>((mouse_screenX / size_offset) + pan_offset_x);
            mouse_worldY = static_cast<float>((mouse_screenY / size_offset) + pan_offset_y);

            int textureX = static_cast<int>(std::floor(mouse_worldX));
            int textureY = static_cast<int>(std::floor(mouse_worldY));

            int upper_textureX = static_cast<int>(mouse_worldX / chunk_width);
            int upper_textureY = static_cast<int>(mouse_worldY / chunk_height);

            int paint_color_r, paint_color_g, paint_color_b;
            std::string txt;

            // Handle mouse button down events
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                int selected_layer_type;

                if(!selected_layer.empty()){
                    selected_layer_type = world.get_layer_type(selected_layer);
                }

                if (const auto& io = ImGui::GetIO(); e.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse) {
                    if(!selected_layer.empty()){
                        if(selected_layer_type==1){ // IS WORLD_LAYER
                            if(editing_map){
                                WorldLayer& referenced_layer = world.get_worldlayer(selected_layer);
                                bool is_upper_layer = referenced_layer.is_upper;

                                IDmap* referenced_idmap = nullptr;
                                for (auto& id_map : world.IDmaps) {
                                    if (id_map.name == referenced_layer.idmap_name) {
                                        if (selected_idmap == referenced_layer.idmap_name) {
                                            referenced_idmap = &id_map;
                                            break;
                                        }
                                    }
                                }

                                if (referenced_idmap) {
                                    auto it = referenced_idmap->id_map.find(selected_tile_id);
                                    if (it != referenced_idmap->id_map.end()) {
                                        std::tie(paint_color_r, paint_color_g, paint_color_b, txt) = it->second;
                                    } else {
                                        std::cerr << "Tile ID " << selected_tile_id << " not found in ID map: " << referenced_layer.idmap_name << std::endl;
                                    }
                                }

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

                                if(referenced_idmap){
                                    lockRect = { locking_coordinate_x - brush_radius, locking_coordinate_y - brush_radius, brush_radius*2+1, brush_radius*2+1 };
                                    if(is_upper_layer){ClampRectToTexture(lockRect, world_width_upper, world_height_upper);}else{ClampRectToTexture(lockRect, world_width_lower, world_height_lower);};
                                    SDL_Texture* referenced_texture = referenced_layer.layer_texture;
                                    SDL_LockTexture(referenced_texture, &lockRect, (void**)&pixels, &pitch);
                                    Uint32 color = (Uint32(paint_color_r) << 24) | (Uint32(paint_color_g) << 16) | (Uint32(paint_color_b) << 8) | Uint32(255);
                                    if(brush_tool==0) PaintBrush(pixels, pitch, brush_radius, color);
                                    if(brush_tool==1) {
                                        Uint32 gotten_target_color = GetPixel(pixels, pitch, brush_radius, brush_radius);
                                        PaintFill(pixels, pitch, brush_radius, gotten_target_color, color);
                                    }

                                    SDL_UnlockTexture(referenced_texture);
                                } else {
                                    std::cout<<"Debug::ReferencedIDmap::Invalid/None"<<std::endl;
                                }
                            }
                        } else if(selected_layer_type==3){
                            if(editing_map){
                                PoliticalLayer referenced_layer = world.get_politicallayer(selected_layer);
                                IDmap* referenced_idmap = nullptr;
                                for (auto& id_map : world.IDmaps) {
                                    if (id_map.name == referenced_layer.idmap_name) {
                                        if (selected_idmap == referenced_layer.idmap_name) {
                                            referenced_idmap = &id_map;
                                            break;
                                        }
                                    }
                                }
                                if (referenced_idmap) {
                                    auto it = referenced_idmap->id_map.find(selected_tile_id);
                                    if (it != referenced_idmap->id_map.end()) {
                                        std::tie(paint_color_r, paint_color_g, paint_color_b, txt) = it->second;
                                    } else {
                                        std::cerr << "Tile ID " << selected_tile_id << " not found in ID map: " << referenced_layer.idmap_name << std::endl;
                                    }
                                }
                                if (upper_textureX < 0 || upper_textureX >= world_width_upper || upper_textureY < 0 || upper_textureY >= world_height_upper) {
                                    printf("Debug::LeftClick::OutOfBounds::(%d, %d)\n", upper_textureX, upper_textureY);
                                    continue;
                                }
                                if(referenced_idmap){
                                    lockRect = { upper_textureX - brush_radius, upper_textureY - brush_radius, brush_radius*2+1, brush_radius*2+1 };
                                    ClampRectToTexture(lockRect, upper_textureX, upper_textureY);
                                    SDL_Texture* referenced_texture = referenced_layer.layer_texture;
                                    SDL_LockTexture(referenced_texture, &lockRect, (void**)&pixels, &pitch);
                                    Uint32 color = (Uint32(paint_color_r) << 24) | (Uint32(paint_color_g) << 16) | (Uint32(paint_color_b) << 8) | Uint32(255);
                                    if(brush_tool==0) PaintBrush(pixels, pitch, brush_radius, color);
                                    if(brush_tool==1) {
                                        Uint32 gotten_target_color = GetPixel(pixels, pitch, brush_radius, brush_radius);
                                        PaintFill(pixels, pitch, brush_radius, gotten_target_color, color);
                                    }

                                    SDL_UnlockTexture(referenced_texture);
                                } else {
                                    std::cout<<"Debug::ReferencedIDmap::Invalid/None"<<std::endl;
                                }
                            }
                        } else if(selected_layer_type==2) { // IS ICON_LAYER
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
                        } else if(selected_linetool == "remove"){
                            for (auto it = referenced_layer.Shapes.begin(); it != referenced_layer.Shapes.end();){
                                it->QueryRemovePoint(mouse_worldX, mouse_worldY);
                                ++it;
                            }
                        }

                        IconCivilian* closest_civilian = nullptr;
                        IconMilitary* closest_military = nullptr;
                        double MinDist = 64.0;
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
                            if (world.selected_world_icon!=nullptr) {
                                world.selected_world_icon->SetSelectionStatus(false);
                            }
                            world.selected_world_icon = closest_civilian;
                            world.selected_world_icon->SetSelectionStatus(true);
                        } else if (closest_military != nullptr){ 
                            if (world.selected_world_icon!=nullptr) {
                                world.selected_world_icon->SetSelectionStatus(false);
                            }
                            world.selected_world_icon = closest_military; 
                            world.selected_world_icon->SetSelectionStatus(true);
                        } else {
                            if (world.selected_world_icon!=nullptr) {
                                world.selected_world_icon->SetSelectionStatus(false);
                            }
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
                        if(selected_layer_type==2){
                            if(pressed_first_line){
                                IconLayer& referenced_layer = world.get_iconlayer(selected_layer);
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
                                        std::tie(paint_color_r, paint_color_g, paint_color_b, txt) = it->second;
                                    } else {
                                        std::cerr << "Tile ID " << selected_tile_id << " not found in ID map: " << selected_idmap << std::endl;
                                    }
                                }
                                referenced_layer.create_shape(temporary_shape, paint_color_r, paint_color_g, paint_color_b, 255);
                                pressed_first_line = false;
                                temporary_shape.ClearPoints();
                            }
                        }
                    }

                    selected_icon_id = 0;
                    selected_decorator_id = 0;
                    popup = !popup;
                    dragging = true;
                    lastMouseX = e.button.x;
                    lastMouseY = e.button.y;
                }
            }

            // Handle mouse drag and motion events
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (const auto& io = ImGui::GetIO(); e.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse) {
                    if(!selected_layer.empty()){
                        int selected_layer_type = world.get_layer_type(selected_layer);
                        if(selected_layer_type==1){ // IS WORLD_LAYER
                            if(editing_map){
                                WorldLayer referenced_layer = world.get_worldlayer(selected_layer);
                                bool is_upper_layer = referenced_layer.is_upper;

                                IDmap* referenced_idmap = nullptr;
                                for (auto& id_map : world.IDmaps) {
                                    if (id_map.name == referenced_layer.idmap_name) {
                                        if (selected_idmap == referenced_layer.idmap_name) {
                                            referenced_idmap = &id_map;
                                            break;
                                        }
                                    }
                                }

                                if (referenced_idmap) {
                                    auto it = referenced_idmap->id_map.find(selected_tile_id);
                                    if (it != referenced_idmap->id_map.end()) {
                                        std::tie(paint_color_r, paint_color_g, paint_color_b, txt) = it->second;
                                    } else {
                                        std::cerr << "Tile ID " << selected_tile_id << " not found in ID map: " << referenced_layer.idmap_name << std::endl;
                                    }
                                }

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

                                if(referenced_idmap){
                                    lockRect = { locking_coordinate_x - brush_radius, locking_coordinate_y - brush_radius, brush_radius*2+1, brush_radius*2+1 };
                                    if(is_upper_layer){ClampRectToTexture(lockRect, world_width_upper, world_height_upper);}else{ClampRectToTexture(lockRect, world_width_lower, world_height_lower);};
                                    SDL_Texture* referenced_texture = referenced_layer.layer_texture;
                                    SDL_LockTexture(referenced_texture, &lockRect, (void**)&pixels, &pitch);
                                    Uint32 color = (Uint32(paint_color_r) << 24) | (Uint32(paint_color_g) << 16) | (Uint32(paint_color_b) << 8) | Uint32(255);
                                    if(brush_tool==0) PaintBrush(pixels, pitch, brush_radius, color);
                                    if(brush_tool==1) {
                                        Uint32 gotten_target_color = GetPixel(pixels, pitch, brush_radius, brush_radius);
                                        PaintFill(pixels, pitch, brush_radius, gotten_target_color, color);
                                    }

                                    SDL_UnlockTexture(referenced_texture);
                                } else {
                                    std::cout<<"Debug::ReferencedIDmap::Invalid/None "<<brush_tool<<std::endl;
                                }
                            }
                        } else if(selected_layer_type==3){
                            if(editing_map){
                                PoliticalLayer referenced_layer = world.get_politicallayer(selected_layer);
                                IDmap* referenced_idmap = nullptr;
                                for (auto& id_map : world.IDmaps) {
                                    if (id_map.name == referenced_layer.idmap_name) {
                                        if (selected_idmap == referenced_layer.idmap_name) {
                                            referenced_idmap = &id_map;
                                            break;
                                        }
                                    }
                                }
                                if (referenced_idmap) {
                                    auto it = referenced_idmap->id_map.find(selected_tile_id);
                                    if (it != referenced_idmap->id_map.end()) {
                                        std::tie(paint_color_r, paint_color_g, paint_color_b, txt) = it->second;
                                    } else {
                                        std::cerr << "Tile ID " << selected_tile_id << " not found in ID map: " << referenced_layer.idmap_name << std::endl;
                                    }
                                }
                                if (upper_textureX < 0 || upper_textureX >= world_width_upper || upper_textureY < 0 || upper_textureY >= world_height_upper) {
                                    printf("Debug::LeftClick::OutOfBounds::(%d, %d)\n", upper_textureX, upper_textureY);
                                    continue;
                                }
                                if(referenced_idmap){
                                    lockRect = { upper_textureX - brush_radius, upper_textureY - brush_radius, brush_radius*2+1, brush_radius*2+1 };
                                    ClampRectToTexture(lockRect, upper_textureX, upper_textureY);
                                    SDL_Texture* referenced_texture = referenced_layer.layer_texture;
                                    SDL_LockTexture(referenced_texture, &lockRect, (void**)&pixels, &pitch);
                                    Uint32 color = (Uint32(paint_color_r) << 24) | (Uint32(paint_color_g) << 16) | (Uint32(paint_color_b) << 8) | Uint32(255);
                                    if(brush_tool==0) PaintBrush(pixels, pitch, brush_radius, color);
                                    if(brush_tool==1) {
                                        Uint32 gotten_target_color = GetPixel(pixels, pitch, brush_radius, brush_radius);
                                        PaintFill(pixels, pitch, brush_radius, gotten_target_color, color);
                                    }

                                    SDL_UnlockTexture(referenced_texture);
                                } else {
                                    std::cout<<"Debug::ReferencedIDmap::Invalid/None"<<std::endl;
                                }
                            }
                        } else if(selected_layer_type==2) { // IS ICON_LAYER
                            std::cout<<"Debug::LeftClickMotion::SelectedLayer::Icon"<<std::endl;
                            if(world.selected_world_icon && moving_icon){
                                auto [icon_width, icon_height] = world.selected_world_icon->GetSize();
                                world.selected_world_icon->SetPosition(mouse_worldX, mouse_worldY);
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
            if (const auto& io = ImGui::GetIO(); e.type == SDL_EVENT_MOUSE_WHEEL && !io.WantCaptureMouse) {
                // Zoom in or out based on the wheel movement
                if(world.selected_world_icon && rotating_icon){
                    if (auto* military_icon = dynamic_cast<IconMilitary*>(world.selected_world_icon)) {
                        military_icon->angle += e.wheel.y * 15;
                    }
                }

                if(rotating_icon==false){
                    if (e.wheel.y > 0) {
                        size_offset += 0.1 * size_offset; // Zoom in
                    } else if (e.wheel.y < 0) {
                        size_offset -= 0.1 * size_offset; // Zoom out
                    }
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
                int w, h;
                SDL_GetRenderOutputSize(renderer, &w, &h);
                current_window_width = w;
                current_window_height = h;
                viewport_output.w = w;
                viewport_output.h = h;
                printf("Window resized to %d x %d\n", viewport_output.w, viewport_output.h);
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
                    ImGui::TextColored(warning_color, "Warning! Max texture(layer) size is 16384, \nexpect a crash or buggy behaviour.");
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
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "World selection");

                if (ImGui::BeginListBox("##worldlist", ImVec2(-FLT_MIN, 10 * ImGui::GetTextLineHeightWithSpacing()))) {
                    std::vector<std::string> discovered_worlds = find_savefiles("saves/");
                    std::vector<std::string> discovered_worlds_internet = find_savefiles_internet();
                    if(discovered_worlds.empty()){
                        ImGui::TextColored(error_color, "No savefiles found in current directory.");
                    } else {
                        if(ImGui::Button("convert from old")){
                            std::ifstream in("a_grid.txt", std::ios::binary);
                            world.set_world_size(600, 300);
                            world.set_chunk_size(20, 20);
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
                        
                            WorldLayer loaded_layer = world.create_worldlayer(renderer, "biome_layer", true, "galaina-tiles");
                            IDmap* referenced_id_map = nullptr;
                            for (auto& id_map : world.IDmaps) {
                                if (id_map.name == "galaina-tiles") {
                                    referenced_id_map = &id_map;
                                    break;
                                }
                            }
                            if (!referenced_id_map) {
                                std::cerr << "IDmap not found: " << "galaina-tiles" << "\n";
                                continue;
                            }

                            void* texPixels;
                            int pitch;
                            if (SDL_LockTexture(loaded_layer.layer_texture, nullptr, &texPixels, &pitch) < 0) {
                                std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                continue;
                            }

                            Uint32* texStart = reinterpret_cast<Uint32*>(texPixels);
                            int width = 600;
                            int height = 300;
                            std::vector<uint8_t> rowBuf(width);

                            for (int y = 0; y < height; y++) {
                                in.read(reinterpret_cast<char*>(rowBuf.data()), width);

                                Uint32* rowOut = reinterpret_cast<Uint32*>(
                                    reinterpret_cast<uint8_t*>(texStart) + (height - 1 - y) * pitch
                                );

                                for (int x = 0; x < width; x++) {
                                    uint8_t idx = rowBuf[x];
                                    rowOut[x] = referenced_id_map->px_LUT[idx];
                                }
                            }

                            SDL_UnlockTexture(loaded_layer.layer_texture);
                            in.close();

                            std::ifstream in_b("b_grid.txt", std::ios::binary);
                            PoliticalLayer loaded_layer_political = world.create_politicallayer(renderer, "political_layer", "galaina-nations", loaded_layer);
                            loaded_layer_political.update_texture(world.IDmaps);
                            IDmap* referenced_id_map_b = nullptr;
                            for (auto& id_map : world.IDmaps) {
                                if (id_map.name == "galaina-nations") {
                                    referenced_id_map_b = &id_map;
                                    break;
                                }
                            }
                            if (!referenced_id_map_b) {
                                std::cerr << "IDmap not found: " << "galaina-nations" << "\n";
                                continue;
                            }

                            void* texPixels_b;
                            int pitch_b;
                            if (SDL_LockTexture(loaded_layer_political.layer_texture, nullptr, &texPixels_b, &pitch_b) < 0) {
                                std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                continue;
                            }

                            Uint32* texStart_b = reinterpret_cast<Uint32*>(texPixels_b);
                            std::vector<uint8_t> rowBuf_b(width);

                            for (int y = 0; y < height; y++) {
                                in_b.read(reinterpret_cast<char*>(rowBuf_b.data()), width);

                                Uint32* rowOut = reinterpret_cast<Uint32*>(
                                    reinterpret_cast<uint8_t*>(texStart_b) + (height - 1 - y) * pitch_b
                                );

                                for (int x = 0; x < width; x++) {
                                    uint8_t idx = rowBuf_b[x];
                                    rowOut[x] = referenced_id_map_b->px_LUT[idx];
                                }
                            }
                            
                            SDL_UnlockTexture(loaded_layer_political.layer_texture);
                            in_b.close();

                            std::ifstream in_d("c_grid.txt", std::ios::binary);
                            WorldLayer loaded_layer_lower = world.create_worldlayer(renderer, "biome_highres", false, "galaina-tiles");
                            IDmap* referenced_id_map_d = nullptr;
                            for (auto& id_map : world.IDmaps) {
                                if (id_map.name == "galaina-tiles") {
                                    referenced_id_map_d = &id_map;
                                    break;
                                }
                            }
                            if (!referenced_id_map_d) {
                                std::cerr << "IDmap not found: " << "galaina-tiles" << "\n";
                                continue;
                            }

                            void* texPixels_d;
                            int pitch_d;
                            if (SDL_LockTexture(loaded_layer_lower.layer_texture, nullptr, &texPixels_d, &pitch_d) < 0) {
                                std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                continue;
                            }

                            Uint32* texStart_d = reinterpret_cast<Uint32*>(texPixels_d);
                            std::vector<uint8_t> rowBuf_d(12000);

                            for (int y = 0; y < 6000; y++) {
                                in_d.read(reinterpret_cast<char*>(rowBuf_d.data()), 12000);

                                Uint32* rowOut = reinterpret_cast<Uint32*>(
                                    reinterpret_cast<uint8_t*>(texStart_d) + (6000 - 1 - y) * pitch_d
                                );

                                for (int x = 0; x < 12000; x++) {
                                    uint8_t idx = rowBuf_d[x];
                                    rowOut[x] = referenced_id_map_d->px_LUT[idx];
                                }
                            }

                            SDL_UnlockTexture(loaded_layer_lower.layer_texture);
                            in_d.close();

                            std::ifstream in_c("d_grid.txt", std::ios::binary);
                            WorldLayer loaded_layer_climate = world.create_worldlayer(renderer, "climate_layer", true, "galaina-climate");
                            IDmap* referenced_id_map_c = nullptr;
                            for (auto& id_map : world.IDmaps) {
                                if (id_map.name == "galaina-climate") {
                                    referenced_id_map_c = &id_map;
                                    break;
                                }
                            }
                            if (!referenced_id_map_c) {
                                std::cerr << "IDmap not found: " << "galaina-climate" << "\n";
                                continue;
                            }

                            void* texPixels_c;
                            int pitch_c;
                            if (SDL_LockTexture(loaded_layer_climate.layer_texture, nullptr, &texPixels_c, &pitch_c) < 0) {
                                std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                continue;
                            }

                            Uint32* texStart_c = reinterpret_cast<Uint32*>(texPixels_c);
                            std::vector<uint8_t> rowBuf_c(width);

                            for (int y = 0; y < height; y++) {
                                in_c.read(reinterpret_cast<char*>(rowBuf_c.data()), width);

                                Uint32* rowOut = reinterpret_cast<Uint32*>(
                                    reinterpret_cast<uint8_t*>(texStart_c) + (height - 1 - y) * pitch_c
                                );

                                for (int x = 0; x < width; x++) {
                                    uint8_t idx = rowBuf_c[x];
                                    rowOut[x] = referenced_id_map_c->px_LUT[idx];
                                }
                            }

                            SDL_UnlockTexture(loaded_layer_climate.layer_texture);
                            in_c.close();


                            {
                            IconLayer& civilian_icon_layer = world.create_iconlayer("civilian_icons");

                            std::ifstream file("civilian_icons.txt");
                            if (!file.is_open())
                            {
                                std::cerr << "Failed to open civilian_icons.txt\n";
                                continue;
                            }

                            float x_, y_;
                            int icon_id_, unique_id;

                            while (file >> x_ >> y_ >> icon_id_ >> unique_id)
                            {
                                civilian_icon_layer.create_civilian_icon(
                                    renderer,
                                    icon_id_+1,
                                    x_,
                                    6000-y_,
                                    world.CivilianIdMap
                                );
                            }

                            }

                            {

                            IconLayer& military_icon_layer = world.create_iconlayer("military_icons");

                            std::ifstream file("military_icons.txt");
                            if (!file.is_open())
                            {
                                std::cerr << "Failed to open military_icons.txt\n";
                                continue;
                            }

                            float x_, y_, rotation;
                            int icon_id_, unique_id, country_id, quality_id;

                            while (file >> x_ >> y_ >> icon_id_ >> unique_id >> country_id >> quality_id)
                            {
                                switch (icon_id_)
                                {
                                    case 0:
                                        icon_id_ = 1;
                                        break; 
                                    case 1:
                                        icon_id_ = 2;
                                        break; 
                                    case 2:
                                        icon_id_ = 3;
                                        break; 
                                    case 3:
                                        icon_id_ = 4;
                                        break; 
                                    case 4:
                                        icon_id_ = 23;
                                        break;
                                    case 5:
                                        icon_id_ = 17; 
                                        break;
                                    case 6:
                                        icon_id_ = 22; 
                                        break;
                                    case 7:
                                        icon_id_ = 15;
                                        break;
                                    case 8:
                                        icon_id_ = 18;
                                        break;
                                    case 9:
                                        icon_id_ = 21;
                                        break;
                                    case 10:
                                        icon_id_ = 19; 
                                        break;
                                    case 11:
                                        icon_id_ = 14; 
                                        break;
                                    case 12:
                                        icon_id_ = 20;
                                        break;
                                    case 13:
                                        icon_id_ = 16;
                                        break;
                                    case 14:
                                        icon_id_ = 5;
                                        break;
                                    case 15:
                                        icon_id_ = 6;
                                        break;
                                    case 16:
                                        icon_id_ = 7;
                                        break;
                                    case 17:
                                        icon_id_ = 8;
                                        break;
                                    case 18:
                                        icon_id_ = 9;
                                        break;
                                    case 19:
                                        icon_id_ = 10;
                                        break;
                                    case 20:
                                        icon_id_ = 11;
                                        break;
                                    case 21:
                                        icon_id_ = 12;
                                        break;
                                    case 22:
                                        icon_id_ = 13;
                                        break;
                                    default:
                                        break;
                                }
                                IconMilitary& returned_icon = military_icon_layer.create_military_icon(
                                    renderer,
                                    icon_id_,
                                    x_,
                                    6000-y_,
                                    world.MilitaryIdMap
                                );

                                returned_icon.angle = rotation;
                            }

                            }
                        }

                        for (const auto& filename : discovered_worlds) {
                            std::string button_filename = "local / " + filename;
                            if(ImGui::Button(button_filename.c_str())){
                                std::string full_filename = "saves/" + filename;
                                std::ifstream in(full_filename, std::ios::binary);
                                if (!in) {
                                    std::cerr << "Failed to open file " << full_filename << "\n";
                                    continue;
                                }

                                int32_t world_width_loaded, world_height_loaded, chunk_width_loaded, chunk_height_loaded;
                                in.read(reinterpret_cast<char*>(&world_width_loaded), sizeof(world_width_loaded));
                                in.read(reinterpret_cast<char*>(&world_height_loaded), sizeof(world_height_loaded));
                                in.read(reinterpret_cast<char*>(&chunk_width_loaded), sizeof(chunk_width_loaded));
                                in.read(reinterpret_cast<char*>(&chunk_height_loaded), sizeof(chunk_height_loaded));
                                world.set_world_size(world_width_loaded, world_height_loaded);
                                world.set_chunk_size(chunk_width_loaded, chunk_height_loaded);
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
                                
                                int32_t num_layers_world, num_layers_icon, num_layers_political;
                                in.read(reinterpret_cast<char*>(&num_layers_world), sizeof(num_layers_world));
                                in.read(reinterpret_cast<char*>(&num_layers_political), sizeof(num_layers_political));
                                in.read(reinterpret_cast<char*>(&num_layers_icon), sizeof(num_layers_icon));

                                std::cout << "Debug::Loading " << num_layers_world << " world layers\n";
                                
                                for (int i = 0; i < num_layers_world; i++) {
                                    // Layer name
                                    int32_t lnameLen;
                                    in.read(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                                    std::string layer_name(lnameLen, '\0');
                                    in.read(layer_name.data(), lnameLen);

                                    // IDmap name
                                    int32_t idnameLen;
                                    in.read(reinterpret_cast<char*>(&idnameLen), sizeof(idnameLen));
                                    std::string idmap_name(idnameLen, '\0');
                                    in.read(idmap_name.data(), idnameLen);

                                    // Dimensions
                                    int32_t width, height;
                                    in.read(reinterpret_cast<char*>(&width), sizeof(width));
                                    in.read(reinterpret_cast<char*>(&height), sizeof(height));

                                    // Upper flag
                                    uint8_t isUpper;
                                    in.read(reinterpret_cast<char*>(&isUpper), sizeof(isUpper));

                                    std::cout << "Debug::LoadingLayer::" << layer_name 
                                            << " (" << width << "x" << height << ") "
                                            << "IDmap=" << idmap_name 
                                            << " Upper=" << (int)isUpper << "\n";

                                    WorldLayer loaded_layer = world.create_worldlayer(renderer, layer_name, isUpper, idmap_name);
                                    IDmap* referenced_id_map = nullptr;
                                    for (auto& id_map : world.IDmaps) {
                                        if (id_map.name == idmap_name) {
                                            referenced_id_map = &id_map;
                                            break;
                                        }
                                    }
                                    if (!referenced_id_map) {
                                        std::cerr << "IDmap not found: " << idmap_name << "\n";
                                        continue;
                                    }

                                    void* texPixels;
                                    int pitch;
                                    if (SDL_LockTexture(loaded_layer.layer_texture, nullptr, &texPixels, &pitch) < 0) {
                                        std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                        continue;
                                    }

                                    Uint32* rowOut = reinterpret_cast<Uint32*>(texPixels);
                                    std::vector<uint8_t> rowBuf(width);

                                    for (int y = 0; y < height; y++) {
                                        in.read(reinterpret_cast<char*>(rowBuf.data()), width);

                                        for (int x = 0; x < width; x++) {
                                            uint8_t idx = rowBuf[x];
                                            rowOut[x] = referenced_id_map->px_LUT[idx];
                                        }

                                        rowOut = reinterpret_cast<Uint32*>(reinterpret_cast<uint8_t*>(rowOut) + pitch);
                                    }

                                    SDL_UnlockTexture(loaded_layer.layer_texture);
                                }

                                for (int i = 0; i < num_layers_political; i++) {
                                    // Layer name
                                    int32_t lnameLen;
                                    in.read(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                                    std::string layer_name(lnameLen, '\0');
                                    in.read(layer_name.data(), lnameLen);

                                    // IDmap name
                                    int32_t idnameLen;
                                    in.read(reinterpret_cast<char*>(&idnameLen), sizeof(idnameLen));
                                    std::string idmap_name(idnameLen, '\0');
                                    in.read(idmap_name.data(), idnameLen);

                                    int32_t lworldlayernameLen;
                                    in.read(reinterpret_cast<char*>(&lworldlayernameLen), sizeof(lworldlayernameLen));
                                    std::string world_layer_name(lworldlayernameLen, '\0');
                                    in.read(world_layer_name.data(), lworldlayernameLen);
                                    WorldLayer& linked_world_layer = world.get_worldlayer(world_layer_name);

                                    // Dimensions
                                    int32_t width, height;
                                    in.read(reinterpret_cast<char*>(&width), sizeof(width));
                                    in.read(reinterpret_cast<char*>(&height), sizeof(height));

                                    std::cout << "Debug::LoadingLayer::" << layer_name 
                                            << " (" << width << "x" << height << ") "
                                            << "IDmap=" << idmap_name << "\n";

                                    PoliticalLayer loaded_layer = world.create_politicallayer(renderer, layer_name, idmap_name, linked_world_layer);
                                    loaded_layer.update_texture(world.IDmaps);
                                    IDmap* referenced_id_map = nullptr;
                                    for (auto& id_map : world.IDmaps) {
                                        if (id_map.name == idmap_name) {
                                            referenced_id_map = &id_map;
                                            break;
                                        }
                                    }
                                    if (!referenced_id_map) {
                                        std::cerr << "IDmap not found: " << idmap_name << "\n";
                                        continue;
                                    }

                                    void* texPixels;
                                    int pitch;
                                    if (SDL_LockTexture(loaded_layer.layer_texture, nullptr, &texPixels, &pitch) < 0) {
                                        std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                        continue;
                                    }

                                    Uint32* rowOut = reinterpret_cast<Uint32*>(texPixels);
                                    std::vector<uint8_t> rowBuf(width);

                                    for (int y = 0; y < height; y++) {
                                        in.read(reinterpret_cast<char*>(rowBuf.data()), width);

                                        for (int x = 0; x < width; x++) {
                                            uint8_t idx = rowBuf[x];
                                            rowOut[x] = referenced_id_map->px_LUT[idx];
                                        }

                                        rowOut = reinterpret_cast<Uint32*>(reinterpret_cast<uint8_t*>(rowOut) + pitch);
                                    }

                                    SDL_UnlockTexture(loaded_layer.layer_texture);
                                }

                                std::cout << "Debug::Loading " << num_layers_icon << " icon layers\n";
                                
                                for (int i = 0; i < num_layers_icon; i++) {
                                    int32_t lnamelen;
                                    in.read(reinterpret_cast<char*>(&lnamelen), sizeof(lnamelen));
                                    std::string layer_name(lnamelen, '\0');
                                    in.read(layer_name.data(), lnamelen);

                                    int32_t num_civilian_icons, num_military_icons, num_shapes;
                                    IconLayer& icon_layer = world.create_iconlayer(layer_name);

                                    in.read(reinterpret_cast<char*>(&num_civilian_icons), sizeof(num_civilian_icons));
                                    for (int j = 0; j < num_civilian_icons; j++) {
                                        int32_t icon_id;
                                        float pos_x, pos_y;
                                        in.read(reinterpret_cast<char*>(&icon_id), sizeof(icon_id));
                                        in.read(reinterpret_cast<char*>(&pos_x), sizeof(pos_x));
                                        in.read(reinterpret_cast<char*>(&pos_y), sizeof(pos_y));
                                        
                                        icon_layer.create_civilian_icon(renderer, icon_id, pos_x, pos_y, world.CivilianIdMap);
                                    }

                                    in.read(reinterpret_cast<char*>(&num_military_icons), sizeof(num_military_icons));
                                    for (int j = 0; j < num_military_icons; j++) {
                                        int32_t icon_id, country_id, quality;
                                        float pos_x, pos_y, angle;
                                        in.read(reinterpret_cast<char*>(&icon_id), sizeof(icon_id));
                                        in.read(reinterpret_cast<char*>(&angle), sizeof(angle));
                                        in.read(reinterpret_cast<char*>(&country_id), sizeof(country_id));
                                        in.read(reinterpret_cast<char*>(&quality), sizeof(quality));
                                        in.read(reinterpret_cast<char*>(&pos_x), sizeof(pos_x));
                                        in.read(reinterpret_cast<char*>(&pos_y), sizeof(pos_y));

                                        icon_layer.create_military_icon(renderer, icon_id, pos_x, pos_y, world.MilitaryIdMap);
                                        auto& created_icon = icon_layer.IconsMilitary.back();
                                        created_icon.angle = angle;

                                        int32_t num_decorators;
                                        in.read(reinterpret_cast<char*>(&num_decorators), sizeof(num_decorators));
                                        for (int k = 0; k < num_decorators; k++) {
                                            int32_t decorator_id;
                                            in.read(reinterpret_cast<char*>(&decorator_id), sizeof(decorator_id));
                                            created_icon.add_decorator(renderer, decorator_id, world.DecoratorIdMap);
                                        }
                                    }

                                    in.read(reinterpret_cast<char*>(&num_shapes), sizeof(num_shapes));
                                    for(int j = 0; j<num_shapes; j++){
                                        int8_t r, g, b, a;
                                        in.read(reinterpret_cast<char*>(&r), sizeof(r));
                                        in.read(reinterpret_cast<char*>(&g), sizeof(g));
                                        in.read(reinterpret_cast<char*>(&b), sizeof(b));
                                        in.read(reinterpret_cast<char*>(&a), sizeof(a));

                                        int32_t num_points;
                                        in.read(reinterpret_cast<char*>(&num_points), sizeof(num_points));

                                        Shape loaded_shape;
                                        for(int k = 0; k<num_points; k++){
                                            float point_x, point_y;
                                            in.read(reinterpret_cast<char*>(&point_x), sizeof(point_x));
                                            in.read(reinterpret_cast<char*>(&point_y), sizeof(point_y));
                                            SDL_FPoint point = {point_x, point_y};
                                            loaded_shape.AddPoint(point);
                                        }
                                        icon_layer.create_shape(loaded_shape, r, g, b, a);
                                    }
                                }

                                in.close();
                            }
                        }
                    }

                    if(discovered_worlds_internet.empty()){
                        ImGui::TextColored(error_color, "No savefiles found in database.");
                    } else {
                        for (const auto& filename : discovered_worlds_internet) {
                            std::string button_filename = "internet / " + filename;
                            if(ImGui::Button(button_filename.c_str())){
                                std::string command = "AWSdownload.exe " + filename;
                                int AWSdownload_exitcode = std::system(command.c_str());
                                if (AWSdownload_exitcode == 0) {
                                    std::cout << "AWSdownload completed successfully: " << AWSdownload_exitcode << std::endl;

                                    std::ifstream in("saves/aws_temp_download.nw", std::ios::binary);
                                    if (!in) {
                                        std::cerr << "Failed to open downloaded file" << "\n";
                                        continue;
                                    }

                                    int32_t world_width_loaded, world_height_loaded, chunk_width_loaded, chunk_height_loaded;
                                    in.read(reinterpret_cast<char*>(&world_width_loaded), sizeof(world_width_loaded));
                                    in.read(reinterpret_cast<char*>(&world_height_loaded), sizeof(world_height_loaded));
                                    in.read(reinterpret_cast<char*>(&chunk_width_loaded), sizeof(chunk_width_loaded));
                                    in.read(reinterpret_cast<char*>(&chunk_height_loaded), sizeof(chunk_height_loaded));
                                    world.set_world_size(world_width_loaded, world_height_loaded);
                                    world.set_chunk_size(chunk_width_loaded, chunk_height_loaded);
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
                                    
                                    int32_t num_layers_world, num_layers_icon, num_layers_political;
                                    in.read(reinterpret_cast<char*>(&num_layers_world), sizeof(num_layers_world));
                                    in.read(reinterpret_cast<char*>(&num_layers_political), sizeof(num_layers_political));
                                    in.read(reinterpret_cast<char*>(&num_layers_icon), sizeof(num_layers_icon));

                                    std::cout << "Debug::Loading " << num_layers_world << " world layers\n";
                                    
                                    for (int i = 0; i < num_layers_world; i++) {
                                        // Layer name
                                        int32_t lnameLen;
                                        in.read(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                                        std::string layer_name(lnameLen, '\0');
                                        in.read(layer_name.data(), lnameLen);

                                        // IDmap name
                                        int32_t idnameLen;
                                        in.read(reinterpret_cast<char*>(&idnameLen), sizeof(idnameLen));
                                        std::string idmap_name(idnameLen, '\0');
                                        in.read(idmap_name.data(), idnameLen);

                                        // Dimensions
                                        int32_t width, height;
                                        in.read(reinterpret_cast<char*>(&width), sizeof(width));
                                        in.read(reinterpret_cast<char*>(&height), sizeof(height));

                                        // Upper flag
                                        uint8_t isUpper;
                                        in.read(reinterpret_cast<char*>(&isUpper), sizeof(isUpper));

                                        std::cout << "Debug::LoadingLayer::" << layer_name 
                                                << " (" << width << "x" << height << ") "
                                                << "IDmap=" << idmap_name 
                                                << " Upper=" << (int)isUpper << "\n";

                                        WorldLayer loaded_layer = world.create_worldlayer(renderer, layer_name, isUpper, idmap_name);
                                        IDmap* referenced_id_map = nullptr;
                                        for (auto& id_map : world.IDmaps) {
                                            if (id_map.name == idmap_name) {
                                                referenced_id_map = &id_map;
                                                break;
                                            }
                                        }
                                        if (!referenced_id_map) {
                                            std::cerr << "IDmap not found: " << idmap_name << "\n";
                                            continue;
                                        }

                                        void* texPixels;
                                        int pitch;
                                        if (SDL_LockTexture(loaded_layer.layer_texture, nullptr, &texPixels, &pitch) < 0) {
                                            std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                            continue;
                                        }

                                        Uint32* rowOut = reinterpret_cast<Uint32*>(texPixels);
                                        std::vector<uint8_t> rowBuf(width);

                                        for (int y = 0; y < height; y++) {
                                            in.read(reinterpret_cast<char*>(rowBuf.data()), width);

                                            for (int x = 0; x < width; x++) {
                                                uint8_t idx = rowBuf[x];
                                                rowOut[x] = referenced_id_map->px_LUT[idx];
                                            }

                                            rowOut = reinterpret_cast<Uint32*>(reinterpret_cast<uint8_t*>(rowOut) + pitch);
                                        }

                                        SDL_UnlockTexture(loaded_layer.layer_texture);
                                    }

                                    for (int i = 0; i < num_layers_political; i++) {
                                        // Layer name
                                        int32_t lnameLen;
                                        in.read(reinterpret_cast<char*>(&lnameLen), sizeof(lnameLen));
                                        std::string layer_name(lnameLen, '\0');
                                        in.read(layer_name.data(), lnameLen);

                                        // IDmap name
                                        int32_t idnameLen;
                                        in.read(reinterpret_cast<char*>(&idnameLen), sizeof(idnameLen));
                                        std::string idmap_name(idnameLen, '\0');
                                        in.read(idmap_name.data(), idnameLen);

                                        int32_t lworldlayernameLen;
                                        in.read(reinterpret_cast<char*>(&lworldlayernameLen), sizeof(lworldlayernameLen));
                                        std::string world_layer_name(lworldlayernameLen, '\0');
                                        in.read(world_layer_name.data(), lworldlayernameLen);
                                        WorldLayer& linked_world_layer = world.get_worldlayer(world_layer_name);

                                        // Dimensions
                                        int32_t width, height;
                                        in.read(reinterpret_cast<char*>(&width), sizeof(width));
                                        in.read(reinterpret_cast<char*>(&height), sizeof(height));

                                        std::cout << "Debug::LoadingLayer::" << layer_name 
                                                << " (" << width << "x" << height << ") "
                                                << "IDmap=" << idmap_name << "\n";

                                        PoliticalLayer loaded_layer = world.create_politicallayer(renderer, layer_name, idmap_name, linked_world_layer);
                                        loaded_layer.update_texture(world.IDmaps);
                                        IDmap* referenced_id_map = nullptr;
                                        for (auto& id_map : world.IDmaps) {
                                            if (id_map.name == idmap_name) {
                                                referenced_id_map = &id_map;
                                                break;
                                            }
                                        }
                                        if (!referenced_id_map) {
                                            std::cerr << "IDmap not found: " << idmap_name << "\n";
                                            continue;
                                        }

                                        void* texPixels;
                                        int pitch;
                                        if (SDL_LockTexture(loaded_layer.layer_texture, nullptr, &texPixels, &pitch) < 0) {
                                            std::cerr << "Failed to lock texture: " << SDL_GetError() << "\n";
                                            continue;
                                        }

                                        Uint32* rowOut = reinterpret_cast<Uint32*>(texPixels);
                                        std::vector<uint8_t> rowBuf(width);

                                        for (int y = 0; y < height; y++) {
                                            in.read(reinterpret_cast<char*>(rowBuf.data()), width);

                                            for (int x = 0; x < width; x++) {
                                                uint8_t idx = rowBuf[x];
                                                rowOut[x] = referenced_id_map->px_LUT[idx];
                                            }

                                            rowOut = reinterpret_cast<Uint32*>(reinterpret_cast<uint8_t*>(rowOut) + pitch);
                                        }

                                        SDL_UnlockTexture(loaded_layer.layer_texture);
                                    }

                                    std::cout << "Debug::Loading " << num_layers_icon << " icon layers\n";
                                    
                                    for (int i = 0; i < num_layers_icon; i++) {
                                        int32_t lnamelen;
                                        in.read(reinterpret_cast<char*>(&lnamelen), sizeof(lnamelen));
                                        std::string layer_name(lnamelen, '\0');
                                        in.read(layer_name.data(), lnamelen);

                                        int32_t num_civilian_icons, num_military_icons, num_shapes;
                                        IconLayer& icon_layer = world.create_iconlayer(layer_name);

                                        in.read(reinterpret_cast<char*>(&num_civilian_icons), sizeof(num_civilian_icons));
                                        for (int j = 0; j < num_civilian_icons; j++) {
                                            int32_t icon_id;
                                            float pos_x, pos_y;
                                            in.read(reinterpret_cast<char*>(&icon_id), sizeof(icon_id));
                                            in.read(reinterpret_cast<char*>(&pos_x), sizeof(pos_x));
                                            in.read(reinterpret_cast<char*>(&pos_y), sizeof(pos_y));
                                            
                                            icon_layer.create_civilian_icon(renderer, icon_id, pos_x, pos_y, world.CivilianIdMap);
                                        }

                                        in.read(reinterpret_cast<char*>(&num_military_icons), sizeof(num_military_icons));
                                        for (int j = 0; j < num_military_icons; j++) {
                                            int32_t icon_id, country_id, quality;
                                            float pos_x, pos_y, angle;
                                            in.read(reinterpret_cast<char*>(&icon_id), sizeof(icon_id));
                                            in.read(reinterpret_cast<char*>(&angle), sizeof(angle));
                                            in.read(reinterpret_cast<char*>(&country_id), sizeof(country_id));
                                            in.read(reinterpret_cast<char*>(&quality), sizeof(quality));
                                            in.read(reinterpret_cast<char*>(&pos_x), sizeof(pos_x));
                                            in.read(reinterpret_cast<char*>(&pos_y), sizeof(pos_y));

                                            icon_layer.create_military_icon(renderer, icon_id, pos_x, pos_y, world.MilitaryIdMap);
                                            auto& created_icon = icon_layer.IconsMilitary.back();
                                            created_icon.angle = angle;

                                            int32_t num_decorators;
                                            in.read(reinterpret_cast<char*>(&num_decorators), sizeof(num_decorators));
                                            for (int k = 0; k < num_decorators; k++) {
                                                int32_t decorator_id;
                                                in.read(reinterpret_cast<char*>(&decorator_id), sizeof(decorator_id));
                                                created_icon.add_decorator(renderer, decorator_id, world.DecoratorIdMap);
                                            }
                                        }

                                        in.read(reinterpret_cast<char*>(&num_shapes), sizeof(num_shapes));
                                        for(int j = 0; j<num_shapes; j++){
                                            int8_t r, g, b, a;
                                            in.read(reinterpret_cast<char*>(&r), sizeof(r));
                                            in.read(reinterpret_cast<char*>(&g), sizeof(g));
                                            in.read(reinterpret_cast<char*>(&b), sizeof(b));
                                            in.read(reinterpret_cast<char*>(&a), sizeof(a));

                                            int32_t num_points;
                                            in.read(reinterpret_cast<char*>(&num_points), sizeof(num_points));

                                            Shape loaded_shape;
                                            for(int k = 0; k<num_points; k++){
                                                float point_x, point_y;
                                                in.read(reinterpret_cast<char*>(&point_x), sizeof(point_x));
                                                in.read(reinterpret_cast<char*>(&point_y), sizeof(point_y));
                                                SDL_FPoint point = {point_x, point_y};
                                                loaded_shape.AddPoint(point);
                                            }
                                            icon_layer.create_shape(loaded_shape, r, g, b, a);
                                        }
                                    }

                                    in.close();
                                    
                                    int status = std::remove("saves/aws_temp_download.nw");
                                    if (status != 0) {
                                        perror("Error deleting temporary savefile :(");
                                    }
                                    else {
                                        std::cout << "Temporary savefile successfully deleted" << std::endl;
                                    }
                                } else {
                                    std::cout << "AWSdownload failed with exit code: " << AWSdownload_exitcode << std::endl;
                                }
                            }
                        }
                    }

                    ImGui::EndListBox();
                }

                ImGui::Separator();
            }

            if(ENABLE_TIPS==true){
                ImGui::TextColored(warning_color, "Disable tips?");
                ImGui::SameLine();
                if(ImGui::Button("yes")){
                    ENABLE_TIPS = false;
                }
            }

            if(world.HasInitializedCheck()){
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "World editing");

                ImGui::InputText("layer name##01",buffer,sizeof(buffer));

                if(ENABLE_TIPS==true){
                    ImGui::TextColored(info_color, "Note: Layers can be Upper or Lower,\nUpper layers are lower resolution and bigger\nLower layers are higher resolution and smaller,\nso we use lower layers for higher detail\nand we use upper layers for the bigger picture.");
                }
                if(ImGui::Button("toggle##02")){
                    upper = !upper;
                }
                ImGui::SameLine();
                ImGui::Text(upper ? "Upper" : "Lower");

                if(ENABLE_TIPS==true){
                    ImGui::TextColored(info_color, "Note: You can choose to create either\nan icon layer or a world layer.");
                }
                if(ImGui::Button("toggle##03")){
                    layer_type = !layer_type;
                }
                ImGui::SameLine();
                ImGui::Text(layer_type ? "Icon" : "World");

                if(ImGui::Button("CREATE layer##04")){
                    if(layer_type){
                        world.create_iconlayer(buffer);
                    } else {
                        if(selected_idmap.empty()){
                            std::cout<<"Debug::NoIDmapSelected::CannotCreateWorldLayer"<<std::endl;
                        } else {
                            world.create_worldlayer(renderer,buffer,upper,selected_idmap);
                        }
                    }
                }

                int selected_layer_type;
                if(!selected_layer.empty()){
                    selected_layer_type = world.get_layer_type(selected_layer);
                }

                if(!selected_layer.empty()){
                    if(selected_layer_type==1){
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Selected layer: %s (World layer)", selected_layer.c_str());

                        if(ImGui::Button("ATTACH political layer##05")){
                            if(selected_idmap.empty()){
                                std::cout<<"Debug::NoIDmapSelected::CannotCreatePoliticalLayer"<<std::endl;
                            } else {
                                WorldLayer& referenced_layer = world.get_worldlayer(selected_layer);
                                PoliticalLayer& returned_layer = world.create_politicallayer(renderer,buffer,selected_idmap,referenced_layer);
                                returned_layer.bake_texture(world.IDmaps);
                                returned_layer.update_texture(world.IDmaps);
                            }
                        }
                    } else if(selected_layer_type==2) {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 1.0f, 1.0f), "Selected layer: %s (Icon layer)", selected_layer.c_str());
                    } else if(selected_layer_type==3) {
                        ImGui::TextColored(ImVec4(0.6f, 0.3f, 8.0f, 1.0f), "Selected layer: %s (Political layer)", selected_layer.c_str());
                    }
                }

                if (ImGui::TreeNode("Layers")) {
                    if (ImGui::TreeNode("Icon Layers")) {
                        if(ENABLE_TIPS==true){
                            ImGui::TextColored(info_color, "Note: Icon layers are rendered from bottom to top,\nthe last layer in the list (the top) is rendered on top.");
                            ImGui::TextColored(info_color, "Each layer can have military and civilian icons,\nit can also contain lines.");
                        }
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

                    if (ImGui::TreeNode("Political Layers")) {
                        const auto& layers = world.GetPoliticalLayers();
                        int total = static_cast<int>(layers.size());

                        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
                            int reverse_index = std::distance(layers.rbegin(), it);
                            int actual_index = total - 1 - reverse_index;

                            std::string layer_name = it->layer_name;

                            if (ImGui::Button(layer_name.c_str())) {
                                selected_layer = layer_name;
                                std::cout << "Debug::SelectedLayer::PoliticalLayer::" << layer_name << std::endl;
                            }

                            ImGui::SameLine();
                            if (ImGui::Button(("toggle##" + layer_name).c_str())) {
                                world.toggle_visibility_layer(layer_name);
                                // Make a non-const reference to call non-const member function
                                PoliticalLayer& nonconst_layer = const_cast<PoliticalLayer&>(*it);
                                nonconst_layer.update_texture(world.IDmaps);
                                std::cout << "Debug::ToggledVisibility::PoliticalLayer::" << layer_name << std::endl;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("+##" + layer_name).c_str())) {
                                world.MovePoliticalLayer(actual_index, false);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("-##" + layer_name).c_str())) {
                                world.MovePoliticalLayer(actual_index, true);
                            }
                            ImGui::SameLine();
                            ImGui::Text(it->visible ? "Visible" : "Hidden");
                        }

                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNode("World Layers")) {
                        if(ENABLE_TIPS==true){
                            ImGui::TextColored(info_color, "Note: World layers are rendered from bottom to top,\nthe last layer in the list (the top) is rendered on top.");
                        }
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

                if(selected_idmap.empty()){
                    ImGui::TextColored(warning_color, "Warning! No Tile ID/ID map selected, \nyou won't be able to create a new world layer.");
                    if(ENABLE_TIPS==true){
                        ImGui::TextColored(info_color, "Select a Tile ID from the dropdown menu,\nlayers get created with the selected ID map,\nso you have to have an ID map selected beforehand.");
                    }
                } else {
                    ImGui::TextColored(info_color, "ID map selected: %s", selected_idmap.c_str());
                    if(ENABLE_TIPS==true){
                        ImGui::TextColored(info_color, "This will create a world layer with this ID map attached.\nIf you want to load this save later,\nmake sure you have this ID map in your IDmaps folder.");
                    }
                }
            }

            if(world.selected_world_icon){
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Icon parameters");

                bool is_military = dynamic_cast<IconMilitary*>(world.selected_world_icon) != nullptr;
                ImGui::Text(is_military ? "Military icon selected" : "Civilian icon selected");

                ImGui::Text("Icon id: %d", world.selected_world_icon->GetIconId());
                ImGui::Text("Icon position: %f %f", world.selected_world_icon->GetPosition().x, world.selected_world_icon->GetPosition().y);
                if(is_military){
                    IconMilitary* military_icon = dynamic_cast<IconMilitary*>(world.selected_world_icon);
                    ImGui::Text("Icon angle: %d", military_icon->angle);
                    ImGui::InputFloat("Set angle", &military_icon->angle);

                    ImGui::Text("Country ID: %d", military_icon->country_id);
                    ImGui::InputInt("Set country ID", &military_icon->country_id);

                    ImGui::Text("Quality: %d", military_icon->quality);
                    ImGui::InputInt("Set quality", &military_icon->quality);

                    if(ENABLE_TIPS==true){
                        ImGui::TextColored(info_color, "Decorators are shown in the order they were added,\nthe last decorator in the list is shown on top.");
                    }
                }

                if(is_military){
                    if(ImGui::Button("Wipe decorators")){
                        world.selected_world_icon->clear_decorators();
                    }
                }
                if(ImGui::Button("Remove icon")){
                    if(!selected_layer.empty()){
                        IconLayer& referenced_layer = world.get_iconlayer(selected_layer);
                        referenced_layer.remove_icon(world.selected_world_icon);
                        world.selected_world_icon = nullptr;
                    }
                }

                ImGui::Separator();
            }

            if(world.HasInitializedCheck()){
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "World saving");

                static char buffer[32] = "";
                ImGui::InputText("save name", buffer, sizeof(buffer));
                
                if(ImGui::Button("Save World")){
                    std::string filename = std::string(buffer) + ".nw";
                    world.SaveWorld(filename, false);
                }

                if(ImGui::Button("Save & Upload")){
                    std::string filename = std::string(buffer) + ".nw";
                    world.SaveWorld(filename, true);
                }
                ImGui::SameLine();
                if(ImGui::Button("< Autofill date")){
                    std::time_t now = std::time(nullptr);
                    std::tm tm_now = *std::localtime(&now);
                    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H_%M_%S", &tm_now);
                }
            }

            ImGui::End();
        }
 
        if (popup==true)
        {
            ImGui::OpenPopup("dropdownmenu");
        }
        if (ImGui::BeginPopup("dropdownmenu"))
        {
            ImGui::Text("popup");
            ImGui::Separator();

            for(auto& id_map : world.IDmaps){
                std::string label = "IDs: " + id_map.name;
                if (ImGui::TreeNode(label.c_str())) {
                    for (int i = 0; i <= 255; ++i) {
                        auto it = id_map.id_map.find(i);
                        if (it == id_map.id_map.end()) {
                            break;
                        } else {
                            auto [r, g, b, text] = it->second;
                            ImVec4 selection_color = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                            if (ImGui::ColorButton(("##" + std::to_string(i)).c_str(), selection_color, 0, ImVec2(32, 16))){
                                printf("Debug::SetID::%d\n", i);
                                selected_tile_id = i;
                                selected_idmap = id_map.name;
                            }
                            ImGui::SameLine(); ImGui::Text("ID:%d", i);
                            if(!text.empty()){
                                ImGui::SameLine(); ImGui::Text("%s", text.c_str());
                            }
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
                        continue;
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
                        continue;
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
                if(ImGui::Button("-##decorator_deselect", ImVec2(32, 32))){
                    selected_decorator_id = 0;
                }
                ImGui::SameLine(); ImGui::Text("Deselect");
                
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

                ImGui::TreePop();
            }

            ImGui::Text("Controls");
            ImGui::Separator();

            ImGui::Checkbox("Move icons", &moving_icon);
            if(ENABLE_TIPS==true){
                ImGui::TextColored(info_color, "Toggle between moving selected icons or not.");
                if(moving_icon==true){
                    ImGui::TextColored(info_color, "You can now move any icon.");
                }
            }

            ImGui::Checkbox("Rotate icons", &rotating_icon);
            if(ENABLE_TIPS==true){
                ImGui::TextColored(info_color, "Toggle between rotating selected icons or not.");
                if(rotating_icon==true){
                    ImGui::TextColored(info_color, "You can now rotate any icon.");
                }
            }

            ImGui::Checkbox("Edit map", &editing_map);
            if(ENABLE_TIPS==true){
                ImGui::TextColored(info_color, "Toggle between interacting or painting the map.");
                if(editing_map==true){
                    ImGui::TextColored(info_color, "You can now paint the map with tiles.");
                }
            }

            static int brushtool_radio = 0;

            if (editing_map==true){
                ImGui::InputInt("Set Brush Size", &brush_radius);

                if (ImGui::RadioButton("Brush", &brushtool_radio, 0)) brush_tool = 0;
                if (ImGui::RadioButton("Fill", &brushtool_radio, 1)) brush_tool = 1;
                // if (ImGui::RadioButton("Line", &brushtool_radio, 2)); //selected_linetool = "remove";
                // if (ImGui::RadioButton("Copy", &brushtool_radio, 3)); //selected_linetool = "remove";
            }

            ImGui::Separator();
            ImGui::Text("Line tools");

            static int linetool_radio = 0;
            if (ImGui::RadioButton("none", &linetool_radio, 0)) selected_linetool = "";
            ImGui::SameLine();
            if (ImGui::RadioButton("add", &linetool_radio, 1)) selected_linetool = "add";
            ImGui::SameLine();
            if (ImGui::RadioButton("delete", &linetool_radio, 2)) selected_linetool = "remove";

            if(ENABLE_TIPS==true){
                ImGui::TextColored(info_color, "You can create lines/shapes by using the line tools below.\nYou can click anywhere to place points\nand when done, you may press right click to finish.");
            }

            // if(ENABLE_TIPS==true){
            //     ImGui::TextColored(info_color, "Click to start placing points for a new shape.");
            // }
            // if(ImGui::Button("Add points")){
            //     selected_linetool = "add";
            // }
            // if(ENABLE_TIPS==true){
            //     ImGui::TextColored(info_color, "Click to start deleting points in shapes.");
            // }
            // if(ImGui::Button("Remove points")){
            //     selected_linetool = "remove";
            // }
            // if(ENABLE_TIPS==true){
            //     ImGui::TextColored(info_color, "Deselect line modification tools.");
            // }
            // if(ImGui::Button("Deselect")){
            //     selected_linetool = "";
            // }
            
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

        viewport_source_upper.x = (float)pan_offset_x / (float)chunk_width;
        viewport_source_upper.y = (float)pan_offset_y / (float)chunk_height;
        viewport_source_upper.w = (float)current_window_width / (float)chunk_width / size_offset;
        viewport_source_upper.h = (float)current_window_height / (float)chunk_height / size_offset;

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
            viewport_output_bounded.w = (intersect.w * size_offset);
            viewport_output_bounded.h = (intersect.h * size_offset);

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

    // "-o",
    // "${workspaceFolder}\\build.exe"

    return 0;
}
