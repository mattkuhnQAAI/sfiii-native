// Exercise the actual native UI with SDL dummy devices, without game code or assets.
#include SF3_HOST_SOURCE
#include "state.h"
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2 && !(argc==3 && std::string(argv[2])=="--display-only")) return 2;
    namespace fs = std::filesystem;
    auto root = fs::path(argv[1]);
    fs::create_directories(root);
    g_cfg.out = root.string();
    g_cfg.record_input = (root / "input.txt").string();
    g_cfg.settings = (root / "settings.ini").string();
    { std::ofstream old(fs::path(std::getenv("SF3_CONTROLLER_DIR")) / "render-resolution.ini"); old << "[display]\ninternal_scale = 4\n"; }
    { std::ofstream f(g_cfg.record_input); f << "100:START,110:,700:A,"; }
    if (!host_open()) return 3;
    if(vdp1_render_scale()!=5 || g_settings.number("display","internal_scale")!=5)return 68;
    aspect_set(false);
    resolution_set(1); // Start repeatably; fixture preferences may be retained.
    State state;
    uint64_t line = 0, frame = 600;
    state(line); state(frame); state.loading = true;
    video_state(state);
    Frame picture; picture.w=320; picture.h=240; picture.px.assign(320*240, 0x102030);
    present(picture);
    // Hidden toolbar has no reserved height and cannot activate invisible controls.
    toolbar_set(2);draw(false);
    if(replay_bar_height()!=0 || g_dst.h<=0 || g_dst.y<0)return 85;
    SDL_Event hidden_click{};hidden_click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;hidden_click.button.button=SDL_BUTTON_LEFT;
    auto hidden_rect=replay_bar_button(3);
    SDL_RenderCoordinatesToWindow(g_ren,hidden_rect.x+hidden_rect.w/2,hidden_rect.y+hidden_rect.h/2,&hidden_click.button.x,&hidden_click.button.y);
    on_event(hidden_click);if(g_menu_open)return 86;
    SDL_Event bar_key{};bar_key.type=SDL_EVENT_KEY_DOWN;bar_key.key.scancode=SDL_SCANCODE_F10;
    on_event(bar_key);if(!toolbar_shown())return 87;
    bar_key.key.repeat=true;on_event(bar_key);if(!toolbar_shown())return 88;
    std::vector<std::string> display_problems;Settings display_saved;bool display_found=false;
    display_saved.read(settings_load(g_cfg.settings,display_found),display_problems);
    if(!display_found || !display_problems.empty() || display_saved.get("display","toolbar")!="shown")return 90;
    toolbar_set(0);draw(false);if(replay_bar_height()<=0)return 89;
    // The same menu path and F11 path share persistence and auto toolbar behavior.
    choose(M_FULLSCREEN,1);SDL_SyncWindow(g_win);
    if(choice_at(M_FULLSCREEN)!=1 || toolbar_shown())return 92;
    bar_key.key.repeat=false;on_event(bar_key);if(!toolbar_shown())return 93;
    toolbar_set(0);bar_key.key.scancode=SDL_SCANCODE_F11;on_event(bar_key);SDL_SyncWindow(g_win);
    if(choice_at(M_FULLSCREEN)!=0 || !toolbar_shown() || g_settings.flag("display","fullscreen"))return 94;
    vsync_set(false);if(choice_at(M_VSYNC)!=0)return 95;
    draw(false);if(picture.px!=std::vector<uint32_t>(320*240,0x102030))return 96;
    for(int mode=0;mode<3;++mode) {
        choose(M_IMAGE_FILTER,mode);draw(false);
        Settings saved_filter;std::vector<std::string> issues;bool found=false;
        saved_filter.read(settings_load(g_cfg.settings,found),issues);
        if(choice_at(M_IMAGE_FILTER)!=mode || !found || !issues.empty() ||
           saved_filter.get("display","image_filter")!=g_settings.get("display","image_filter") ||
           picture.px!=std::vector<uint32_t>(320*240,0x102030))return 100;
    }
    choose(M_IMAGE_FILTER,1);
    if(argc==3) {
        if(!g_gpu_active)return 97;
        choose(M_VSYNC,1);if(choice_at(M_VSYNC)!=1 || !g_settings.flag("display","vsync"))return 98;
        draw(true);choose(M_VSYNC,0);if(choice_at(M_VSYNC)!=0)return 99;
        std::printf("PASS: GPU fullscreen, toolbar, hidden click, preference persistence and VSync on/off (%s, %s)\n",g_gpu.backend(),g_gpu.adapter());
        return 0;
    }
    SDL_Event event{};
    event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_F5;
    on_event(event);
    auto checkpoint = sf3_replay::latest(std::getenv("SF3_REPLAY_ROOT"));
    if (sf3_replay::verify(checkpoint, std::getenv("SF3_REPLAY_SEED")) != 600) return 4;
    auto click = [&](int button) {
        SDL_Event mouse{}; mouse.type=SDL_EVENT_MOUSE_BUTTON_DOWN; mouse.button.button=SDL_BUTTON_LEFT;
        auto rect = replay_bar_button(button);
        SDL_RenderCoordinatesToWindow(g_ren, rect.x+rect.w/2, rect.y+rect.h/2, &mouse.button.x, &mouse.button.y);
        on_event(mouse);
    };
    auto initial_pixels=picture.px;
    if(g_game_speed!=1 || !host_audio_open())return 46;
    for(int cycle=0;cycle<6;++cycle) {
        click(7);int expected=cycle%3+2;if(expected==4)expected=1;
        if(g_game_speed!=expected || SDL_GetAudioStreamFrequencyRatio(g_audio)!=expected ||
           SDL_GetAudioStreamQueued(g_audio)!=0 || g_audio_buffer.playing ||
           g_audio_buffer.speed!=expected || picture.px!=initial_pixels || vdp1_render_scale()!=1)return 47;
        std::vector<int16_t> pcm(sf3::AudioBuffer::lead*2,123);
        host_audio_push(pcm.data(),sf3::AudioBuffer::lead);
        if(g_audio_buffer.playing!=(expected==1))return 48;
    }
    event.key.scancode=SDL_SCANCODE_F6;on_event(event);
    if(g_game_speed!=2)return 49;
    event.key.repeat=true;on_event(event);if(g_game_speed!=2)return 50;
    event.key.repeat=false;menu_adjust(M_SPEED,1);if(g_game_speed!=3)return 51;
    menu_adjust(M_SPEED,1);if(g_game_speed!=1)return 52;
    speed_set(0);speed_set(4);if(g_game_speed!=1)return 53;
    // A speed change inside a paused menu must not add the pause twice.
    menu_open();SDL_Event resume{};resume.type=SDL_EVENT_KEY_DOWN;
    resume.key.scancode=SDL_SCANCODE_F6;SDL_PushEvent(&resume);
    resume.key.scancode=SDL_SCANCODE_ESCAPE;SDL_PushEvent(&resume);
    menu_loop();
    auto due=g_base+std::chrono::nanoseconds(sat_now()/g_game_speed);
    if(g_game_speed!=2 || g_menu_open || std::chrono::abs(Clock::now()-due)>std::chrono::milliseconds(30))return 54;
    speed_set(1);
    // Even on a field with no host composition, controls must remain responsive.
    State skipped;uint64_t skipped_line=0,skipped_frame=601;
    skipped(skipped_line);skipped(skipped_frame);skipped.loading=true;video_state(skipped);
    speed_set(2);if(host_wants_frame())return 55;
    resume.key.scancode=SDL_SCANCODE_F6;SDL_PushEvent(&resume);host_pace(0);
    if(g_game_speed!=3)return 56;
    State shown;skipped_frame=600;shown(skipped_line);shown(skipped_frame);shown.loading=true;video_state(shown);
    if(!host_wants_frame())return 57;
    speed_set(1);
    click(5);
    if(vdp1_render_scale()!=2 || picture.px!=initial_pixels)return 20;
    click(6);if(vdp1_render_scale()!=5 || choice_at(M_RESOLUTION)!=2)return 41;
    if(choice_count(M_RESOLUTION)!=3 || std::string(choice_name(M_RESOLUTION,2))!="5x")return 21;
    load_settings(g_cfg.settings);if(vdp1_render_scale()!=5)return 42;
    for(int i=0;i<5+kRenderScaleCount;++i) {
        auto a=replay_bar_button(i);
        if(a.x<0 || a.x+a.w>g_out_w)return 43;
        if(i && replay_bar_button(i-1).x+replay_bar_button(i-1).w>a.x)return 44;
    }
    click(4);if(vdp1_render_scale()!=1)return 22;
    click(5);
    click(0);
    auto second = sf3_replay::latest(std::getenv("SF3_REPLAY_ROOT"));
    if (second == checkpoint) return 5;
    event.key.scancode=SDL_SCANCODE_F8;
    on_event(event);
    click(2);
    auto work = fs::path(std::getenv("SF3_CONTROLLER_DIR"));
    std::ifstream pointer(work / "last-capture.txt");
    std::string capture_path((std::istreambuf_iterator<char>(pointer)), {});
    for (const char* file : {"game.png", "window.png", "context.txt", "capture.json", "input.txt", "wram-high.bin", "wram-low.bin", "dump-600.bin"})
        if (!fs::exists(fs::path(capture_path) / file)) return 12;
    if (fs::file_size(fs::path(capture_path) / "wram-high.bin") != 0x100000) return 13;
    if (fs::file_size(fs::path(capture_path) / "dump-600.bin") != 0x141220) return 14;
    click(3);
    if (!g_menu_open) return 6;
    auto layout = menu_layout();
    for(auto id:{M_FULLSCREEN,M_TOOLBAR,M_VSYNC,M_IMAGE_FILTER})
        if(std::none_of(layout.rows.begin(),layout.rows.end(),[&](const MenuRow& row){return row.id==id;}))return 91;
    bool save=false, load=false, has_capture=false;
    for (auto& row : layout.rows) { save |= row.id==M_REPLAY_SAVE; load |= row.id==M_REPLAY_LOAD; has_capture |= row.id==M_CAPTURE; }
    if (!save || !load || !has_capture) return 7;
    auto resolution_row=std::find_if(layout.rows.begin(),layout.rows.end(),[](const MenuRow& r){return r.id==M_RESOLUTION;});
    if(resolution_row==layout.rows.end() || resolution_row->control!=Control::Choice)return 27;
    auto aspect_row=std::find_if(layout.rows.begin(),layout.rows.end(),[](const MenuRow& r){return r.id==M_ASPECT;});
    if(aspect_row==layout.rows.end() || aspect_row->control!=Control::Choice || choice_count(M_ASPECT)!=2)return 36;
    menu_adjust(M_ASPECT,1);if(!vdp1_widescreen() || choice_at(M_ASPECT)!=1 || picture.px!=initial_pixels)return 37;
    load_settings(g_cfg.settings);if(!vdp1_widescreen())return 38;
    resolution_set(1);if(g_gpu_active && !vdp1_gpu_active())return 39;
    aspect_set(false);if(vdp1_widescreen() || vdp1_gpu_active())return 40;
    resolution_set(2);
    for(const auto& row:layout.rows)if(std::string(menu_label(row.id)).find("HD")!=std::string::npos)return 28;
    menu_adjust(M_RESOLUTION,1);if(vdp1_render_scale()!=5)return 29;
    {std::ofstream saved(g_cfg.settings);saved<<"[display]\ninternal_scale = 1\n";}
    load_settings(g_cfg.settings);if(vdp1_render_scale()!=5)return 30;
    menu_adjust(M_RESOLUTION,1);if(vdp1_render_scale()!=1)return 31;
    menu_adjust(M_RESOLUTION,1);if(vdp1_render_scale()!=2)return 32;
    upload_frame(picture);
    auto visible=[&](MenuId id){return std::any_of(layout.rows.begin(),layout.rows.end(),[&](const MenuRow& r){return r.id==id;});};
    if(visible(M_AA) || visible(M_RAY))return 58;
    if(visible(M_AI_GRAPHICS)!=(g_ai_ready[0] && g_ai_ready[1] && g_ai_ready[2]))return 69;
    if(!visible(M_AI_GRAPHICS) && g_ai_mode!=0)return 83;
    if(std::string(choice_name(M_AI_GRAPHICS,0))!="Off" ||
       std::string(choice_name(M_AI_GRAPHICS,1))!="Animated" ||
       std::string(choice_name(M_AI_GRAPHICS,2))!="Illustrated" ||
       std::string(choice_name(M_AI_GRAPHICS,3))!="General")return 84;
    if(visible(M_AI_GRAPHICS)) {
        auto persisted=[&](const char* mode) {
            bool found=false;std::vector<std::string> issues;Settings reloaded;
            reloaded.read(settings_load(g_cfg.settings,found),issues);
            return found && issues.empty() && reloaded.get("display","ai_graphics")==mode;
        };
        ai_set(0);if(choice_at(M_AI_GRAPHICS)!=0)return 71;
        if(!persisted("off"))return 79;
        menu_adjust(M_AI_GRAPHICS,1);
        if(choice_at(M_AI_GRAPHICS)!=1 || picture.px!=initial_pixels)return 72;
        if(!persisted("video"))return 80;
        resolution_set(1);
        if(!gpu_picture_active() || !g_gpu_tex || !g_ai_tex || picture.px!=initial_pixels)return 78;
        resolution_set(2);
        menu_adjust(M_AI_GRAPHICS,1);
        if(choice_at(M_AI_GRAPHICS)!=2 || picture.px!=initial_pixels)return 73;
        if(!persisted("anime"))return 81;
        menu_adjust(M_AI_GRAPHICS,1);
        if(choice_at(M_AI_GRAPHICS)!=3 || picture.px!=initial_pixels)return 74;
        if(!persisted("general"))return 82;
        const auto row=std::find_if(layout.rows.begin(),layout.rows.end(),[](const MenuRow& r){return r.id==M_AI_GRAPHICS;});
        const auto area=control_area(layout,*row);
        if(ui_text_width(14*layout.u,choice_name(M_AI_GRAPHICS,3),true)>area.w-40*layout.u)return 75;
        menu_tap(area.x+area.w-4*layout.u,area.y+area.h/2,0);
        if(choice_at(M_AI_GRAPHICS)!=0)return 76;
        menu_tap(area.x+4*layout.u,area.y+area.h/2,0);
        if(choice_at(M_AI_GRAPHICS)!=3)return 77;
        ai_set(0);
    }
    if(choice_at(M_AA)!=0)return 70;
    aa_set(true);
    if(choice_at(M_AA)!=(g_gpu_active?1:0) || picture.px!=initial_pixels)return 59;
    if(g_gpu_active) {
        Frame aa; if(!g_gpu.capture(aa))return 60;
        for(auto p:aa.px)if(p!=0x102030)return 61;
    }
    aa_set(false);if(choice_at(M_AA)!=0)return 62;
    if(choice_at(M_RAY)!=0)return 63;
    ray_set(true);
    if(choice_at(M_RAY)!=(g_gpu_active?1:0) || picture.px!=initial_pixels)return 64;
    if(g_gpu_active) {
        Frame ray; if(!g_gpu.capture(ray))return 65;
        for(auto p:ray.px)if(p!=0x102030)return 66;
    }
    ray_set(false);if(choice_at(M_RAY)!=0)return 67;
    if(vdp1_gpu_active()) {
        float w=0,h=0;if(!g_gpu_tex || !SDL_GetTextureSize(g_gpu_tex,&w,&h) || w!=640 || h!=480)return 33;
        Frame readback;if(!g_gpu.capture(readback) || readback.w!=640 || readback.h!=480)return 34;
        for(auto p:readback.px)if(p!=0x102030)return 35;
    }
    menu_activate(M_REPLAY_SAVE);
    menu_activate(M_CAPTURE);
    draw(false);
    SDL_Surface* surface = SDL_RenderReadPixels(g_ren, nullptr);
    if (!surface) return 8;
    SDL_Surface* rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
    if (!rgb) return 9;
    Frame capture; capture.w=rgb->w; capture.h=rgb->h; capture.px.resize(rgb->w*rgb->h);
    for (int y=0; y<rgb->h; ++y) std::memcpy(capture.px.data()+y*rgb->w, (uint8_t*)rgb->pixels+y*rgb->pitch, rgb->w*4);
    write_png((root / "menu.png").string(), capture);
    SDL_DestroySurface(rgb); SDL_DestroySurface(surface);
    menu_close();
    bool stopped=false;
    try { click(1); } catch (...) { stopped=true; }
    if (!stopped || !fs::exists(root / "reload-request.txt")) return 10;
    fs::remove(root / "reload-request.txt");
    auto last = sf3_replay::latest(std::getenv("SF3_REPLAY_ROOT"));
    { std::ofstream changed(last / "input.txt", std::ios::app); changed << "damaged"; }
    stopped=false;
    try { click(1); } catch (...) { stopped=true; }
    if (stopped || fs::exists(root / "reload-request.txt")) return 11;
    std::printf("PASS: F5/F8, toolbar save/load/capture, menu, capture files, reload, damaged load keeps running\n");
    return 0;
}
