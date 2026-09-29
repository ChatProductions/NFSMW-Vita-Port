#pragma once
#include <vita2d.h>

struct RetailFrontendAssets {
    vita2d_texture *background = nullptr;
    char background_name[64]{};
    bool frontb_ok = false;
    bool fronta_ok = false;
};

RetailFrontendAssets LoadRetailFrontendAssets();
void FreeRetailFrontendAssets(RetailFrontendAssets &assets);
