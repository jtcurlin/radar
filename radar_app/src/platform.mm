// src/platform.mm

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#import <glob.h>

#include "platform.hpp"
#include "radar.hpp"
#include "renderer.hpp"
#include "controller.hpp"

@interface RadarView : NSView
@property (nonatomic, strong) CAMetalLayer* metalLayer;
@end
@implementation RadarView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        _metalLayer = (CAMetalLayer*)self.layer;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _metalLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
    }
    return self;
}
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
@end


// main application delegate
@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) RadarView* radarView;
@property (nonatomic, strong) NSTimer* renderTimer;

// ui controls
@property (nonatomic, strong) NSPopUpButton *serialPortPopUp;
@property (nonatomic, strong) NSButton *connectButton;
@property (nonatomic, strong) NSButton *clearButton;
@property (nonatomic, strong) NSTextField *ipAddressField; 
@end

@implementation AppDelegate
{
    std::unique_ptr<Renderer> _renderer;
    std::shared_ptr<RadarModel> _radarModel;
    std::unique_ptr<Controller> _controller;
}

- (NSArray<NSString *> *)findSerialPorts {
    glob_t globResult;
    if (glob("/dev/cu.*", GLOB_TILDE, NULL, &globResult) != 0) { return @[]; }
    NSMutableArray<NSString *> *ports = [NSMutableArray array];
    for (size_t i = 0; i < globResult.gl_pathc; ++i) {
        [ports addObject:[NSString stringWithUTF8String:globResult.gl_pathv[i]]];
    }
    globfree(&globResult);
    return ports;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"Fatal: Metal is not supported on this device.");
        [NSApp terminate:nil];
        return;
    }

    NSRect frame = NSMakeRect(0, 0, 900, 700);
    
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];

    NSView *mainContainer = [[NSView alloc] initWithFrame:frame];
    NSView *controlBar = [[NSView alloc] init];
    self.radarView = [[RadarView alloc] init];
    
    controlBar.translatesAutoresizingMaskIntoConstraints = NO;
    self.radarView.translatesAutoresizingMaskIntoConstraints = NO;
    
    [mainContainer addSubview:controlBar];
    [mainContainer addSubview:self.radarView];
    
    self.window.contentView = mainContainer;
    self.radarView.metalLayer.device = device;
    [self.window makeKeyAndOrderFront:nil];
    
    // --- create ui controls ---
    self.serialPortPopUp = [NSPopUpButton new];
    self.connectButton = [NSButton buttonWithTitle:@"Connect" target:self action:@selector(toggleConnection:)];
    self.clearButton = [NSButton buttonWithTitle:@"Clear" target:self action:@selector(clearDetections:)];
    self.ipAddressField = [NSTextField textFieldWithString:@"192.168.1.100"]; // Default IP
    NSButton *setIpButton = [NSButton buttonWithTitle:@"Set IP" target:self action:@selector(setRadarIP:)];

    NSArray<NSView *> *controls = @[self.serialPortPopUp, self.connectButton, self.clearButton, self.ipAddressField, setIpButton];
    for (NSView *control in controls) {
        control.translatesAutoresizingMaskIntoConstraints = NO;
        [controlBar addSubview:control];
    }
    
    // --- layout ---
    NSDictionary *views = @{ @"controlBar": controlBar, @"radarView": self.radarView };
    [mainContainer addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[controlBar]|" options:0 metrics:nil views:views]];
    [mainContainer addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[radarView]|" options:0 metrics:nil views:views]];
    [mainContainer addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[controlBar(40)][radarView]|" options:0 metrics:nil views:views]];

    NSDictionary *controlViews = @{ @"popup": self.serialPortPopUp, @"connect": self.connectButton, @"clear": self.clearButton, @"ipField": self.ipAddressField, @"setIp": setIpButton };
    [controlBar addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|-10-[popup(200)]-10-[connect]-5-[clear]" options:NSLayoutFormatAlignAllCenterY metrics:nil views:controlViews]];
    [controlBar addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:[ipField(120)]-5-[setIp]-10-|" options:NSLayoutFormatAlignAllCenterY metrics:nil views:controlViews]];
    
    // --- populate & finalize ---
    [self.serialPortPopUp addItemsWithTitles:[self findSerialPorts]];
    
    _radarModel = std::make_shared<RadarModel>();
    _controller = std::make_unique<Controller>(_radarModel);
    _renderer = std::make_unique<Renderer>(device, _radarModel, self.radarView.metalLayer);
    
    self.renderTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0 target:self selector:@selector(mainTick:) userInfo:nil repeats:YES];
}

- (void)toggleConnection:(id)sender {
    if (self.connectButton.state == NSControlStateValueOn) {
        NSString *selectedPort = self.serialPortPopUp.titleOfSelectedItem;
        if (!selectedPort || selectedPort.length == 0) {
            NSLog(@"No serial port selected.");
            self.connectButton.state = NSControlStateValueOff;
            return;
        }
        _controller->connect_control_unit([selectedPort UTF8String]);
        self.connectButton.title = @"Disconnect";
        self.serialPortPopUp.enabled = NO;
    } else {
        _controller->disconnect_control_unit();
        self.connectButton.title = @"Connect";
        self.serialPortPopUp.enabled = YES;
    }
}

- (void)setRadarIP:(id)sender {
    _controller->set_radar_unit_ip([self.ipAddressField.stringValue UTF8String]);
}

- (void)clearDetections:(id)sender {
    if (_radarModel) {
        _radarModel->clear_hits();
    }
}

- (void)mainTick:(NSTimer*)timer {
    @autoreleasepool {
        if (_controller) {
            _controller->tick();
        }
        if (_renderer) {
            _renderer->draw();
        }
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    _controller.reset(); // this will disconnect serial port
    [self.renderTimer invalidate];
    self.renderTimer = nil;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

@end

Platform::Platform() {
    @autoreleasepool {
        NSApplication.sharedApplication.activationPolicy = NSApplicationActivationPolicyRegular;
        m_delegate = [AppDelegate new];
        NSApplication.sharedApplication.delegate = m_delegate;
        [NSApp activateIgnoringOtherApps:YES];
    }
}
Platform::~Platform() = default;
void Platform::run() {
    [NSApp run];
}
