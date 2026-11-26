#pragma once
#include <pu/Plutonium>
#include <algorithm>
#include <cstdio>
#include "nx/nexus_usb.hpp"

namespace inst::ui {
class UsbSpaceCard {
    using Text = pu::ui::elm::TextBlock;
    using Bar = pu::ui::elm::Rectangle;
    Text::Ref label, space;
    Bar::Ref track, fill;
public:
    void attach(pu::ui::Layout& page) {
        label = Text::New(0, 24, "USB Drive", 16);
        space = Text::New(0, 52, "Checking...", 16);
        label->SetColor({255,255,255,255});
        space->SetColor({255,255,255,255});
        track = Bar::New(0, 42, 148, 6, pu::ui::Color{255,255,255,51});
        fill = Bar::New(0, 42, 0, 6, pu::ui::Color{140,112,255,255});
        page.Add(track); page.Add(fill); page.Add(label); page.Add(space);
    }
    void update(int x, int width, const nx::nexus::Drive& drive) {
        using nx::nexus::DriveState;
        label->SetX(x); space->SetX(x); track->SetX(x); fill->SetX(x);
        track->SetWidth(width);
        char text[48]{};
        int used = 0;
        switch (drive.state) {
            case DriveState::Ready:
                if (drive.total) {
                    std::snprintf(text,sizeof(text),"%.1f GiB free",drive.free / 1073741824.0);
                    used = static_cast<int>(width * (1.0 - static_cast<double>(drive.free) / drive.total));
                } else std::snprintf(text,sizeof(text),"Space unavailable");
                break;
            case DriveState::Disconnected: std::snprintf(text,sizeof(text),"Disconnected"); break;
            case DriveState::Starting: std::snprintf(text,sizeof(text),"Connecting..."); break;
            case DriveState::Error: std::snprintf(text,sizeof(text),"Check drive"); break;
            default: std::snprintf(text,sizeof(text),"Nexus unavailable"); break;
        }
        space->SetText(text);
        fill->SetWidth(std::clamp(used,0,width));
    }
};
}
