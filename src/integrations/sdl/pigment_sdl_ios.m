/**
 * Copyright 2026 Angel-Leduc TA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)

    #import <UIKit/UIKit.h>
    #import <QuartzCore/CAMetalLayer.h>

    #define PIGMENT_PROP_SDL_METAL_VIEW "PIGMENT.SDL.metal_view"

@interface PigmentSDLMetalView : UIView

@property(nonatomic) BOOL highPixelDensity;

- (void)updateContentScale;

@end

@implementation PigmentSDLMetalView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if(self != nil)
    {
        [self registerForTraitChanges:@[[UITraitDisplayScale class]]
                          withHandler:^(PigmentSDLMetalView* view, UITraitCollection* previous) {
                            (void) previous;
                            [view updateContentScale];
                          }];
    }

    return self;
}

- (void)updateContentScale
{
    // Match SDL's pixel density, including windows without HIGH_PIXEL_DENSITY.
    CGFloat scale = self.highPixelDensity ? self.window.screen.nativeScale : 1.0;
    if(scale > 0.0 && self.contentScaleFactor != scale)
    {
        self.contentScaleFactor = scale;
    }
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    [self updateContentScale];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    [self updateContentScale];
}

@end

static void SDLCALL destroy_metal_view(void* userdata, void* value)
{
    (void) userdata;
    @autoreleasepool
    {
        PigmentSDLMetalView* view = CFBridgingRelease(value);
        [view removeFromSuperview];
    }
}

void* pigment_sdl_ios_metal_layer(SDL_Window* window)
{
    if(window == NULL || ![NSThread isMainThread])
    {
        SDL_SetError("Pigment SDL iOS Metal views must be created on the main thread with a valid window");
        return NULL;
    }

    @autoreleasepool
    {
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        if(props == 0)
        {
            return NULL;
        }

        PigmentSDLMetalView* view = (__bridge PigmentSDLMetalView*) SDL_GetPointerProperty(props, PIGMENT_PROP_SDL_METAL_VIEW, NULL);
        if(view != nil)
        {
            return (__bridge void*) view.layer;
        }

        UIWindow* ui_window = (__bridge UIWindow*) SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
        UIView* parent      = ui_window.rootViewController.view;
        if(parent == nil)
        {
            SDL_SetError("Pigment SDL could not find the iOS window's SDL view");
            return NULL;
        }

        int width = 0, height = 0;
        if(!SDL_GetWindowSizeInPixels(window, &width, &height))
        {
            return NULL;
        }

        view = [[PigmentSDLMetalView alloc] initWithFrame:parent.bounds];
        if(view == nil)
        {
            SDL_OutOfMemory();
            return NULL;
        }

        view.highPixelDensity       = (SDL_GetWindowFlags(window) & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0;
        view.autoresizingMask       = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        view.userInteractionEnabled = NO;
        view.opaque                 = (SDL_GetWindowFlags(window) & SDL_WINDOW_TRANSPARENT) == 0;
        [parent addSubview:view];

        // Initialize drawableSize once. Only MoltenVK changes it afterwards.
        if(width > 0 && height > 0)
        {
            ((CAMetalLayer*) view.layer).drawableSize = CGSizeMake(width, height);
        }

        // Window properties own the view, including cleanup on registration failure.
        if(!SDL_SetPointerPropertyWithCleanup(props, PIGMENT_PROP_SDL_METAL_VIEW, (__bridge_retained void*) view, destroy_metal_view, NULL))
        {
            return NULL;
        }

        return (__bridge void*) view.layer;
    }
}

#endif
