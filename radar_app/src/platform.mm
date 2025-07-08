// src/platform.mm

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#import <glob.h>

#include "controller.hpp"
#include "platform.hpp"
#include "radar.hpp"
#include "renderer.hpp"

@interface RadarView : NSView
@property (nonatomic, strong) CAMetalLayer *metalLayer;
@end

@implementation RadarView
+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.wantsLayer = YES;
        _metalLayer = (CAMetalLayer *)self.layer;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _metalLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
    }
    return self;
}

- (CALayer *)makeBackingLayer
{
    return [CAMetalLayer layer];
}
@end

// main application delegate
@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) RadarView *radarView;
@property (nonatomic, strong) NSTimer *renderTimer;

// ui controls
@property (nonatomic, strong) NSPopUpButton *serialPortPopUp;
@property (nonatomic, strong) NSButton *connectButton;
@property (nonatomic, strong) NSButton *clearButton;
@property (nonatomic, strong) NSTextField *ipAddressFieldLabel;
@property (nonatomic, strong) NSTextField *ipAddressField;

@property (nonatomic, strong) NSSlider *angularResSlider;
@property (nonatomic, strong) NSSlider *radialResSlider;
@end

@implementation AppDelegate
{
    std::unique_ptr<Renderer> _renderer;
    std::shared_ptr<RadarModel> _radarModel;
    std::unique_ptr<Controller> _controller;
}

- (NSArray<NSString *> *)findSerialPorts
{
    glob_t globResult;
    if (glob("/dev/cu.*", GLOB_TILDE, NULL, &globResult) != 0)
    {
        return @[];
    }
    NSMutableArray<NSString *> *ports = [NSMutableArray array];
    for (size_t i = 0; i < globResult.gl_pathc; ++i)
    {
        [ports addObject:[NSString stringWithUTF8String:globResult.gl_pathv[i]]];
    }
    globfree(&globResult);
    return ports;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
    {
        NSLog(@"Fatal: Metal is not supported on this device.");
        [NSApp terminate:nil];
        return;
    }

    NSRect frame = NSMakeRect(0, 0, 900, 700);

    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [self setupMainMenu];

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
    self.ipAddressFieldLabel = [NSTextField labelWithString:@"Controller Configuration"];
    self.ipAddressField = [NSTextField textFieldWithString:@"192.168.1.100"];
    NSButton *setIpButton = [NSButton buttonWithTitle:@"Set IP" target:self action:@selector(setRadarIP:)];

    NSTextField *radarDimsControlsLabel = [NSTextField labelWithString:@"Radar Dimensions"];
    NSTextField *angularSliderLabel = [NSTextField labelWithString:@"Angular"];
    NSTextField *radialSliderLabel = [NSTextField labelWithString:@"Radial"];
    self.angularResSlider = [NSSlider sliderWithValue:30
                                             minValue:30
                                             maxValue:100
                                               target:self
                                               action:@selector(resSliderChanged:)];
    self.angularResSlider.numberOfTickMarks = 7;
    self.angularResSlider.allowsTickMarkValuesOnly = YES;

    self.radialResSlider = [NSSlider sliderWithValue:4
                                            minValue:2
                                            maxValue:9
                                              target:self
                                              action:@selector(resSliderChanged:)];
    self.radialResSlider.numberOfTickMarks = 7;
    self.radialResSlider.allowsTickMarkValuesOnly = YES;

    // --- layout ---
    NSDictionary *mainViews = @{@"controlBar" : controlBar, @"radarView" : self.radarView};

    [mainContainer addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[controlBar]|"
                                                                          options:0
                                                                          metrics:nil
                                                                            views:mainViews]];
    [mainContainer addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[radarView]|"
                                                                          options:0
                                                                          metrics:nil
                                                                            views:mainViews]];
    [mainContainer addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[radarView][controlBar(200)]|"
                                                                          options:0
                                                                          metrics:nil
                                                                            views:mainViews]];

    NSArray<NSView *> *controls = @[
        self.serialPortPopUp, self.connectButton, self.clearButton, self.ipAddressFieldLabel, self.ipAddressField,
        setIpButton, radarDimsControlsLabel, angularSliderLabel, self.angularResSlider, radialSliderLabel,
        self.radialResSlider
    ];

    for (NSView *control in controls)
    {
        control.translatesAutoresizingMaskIntoConstraints = NO;
        [controlBar addSubview:control];
        [control addConstraint:[NSLayoutConstraint constraintWithItem:control
                                                            attribute:NSLayoutAttributeWidth
                                                            relatedBy:NSLayoutRelationEqual
                                                               toItem:nil
                                                            attribute:NSLayoutAttributeNotAnAttribute
                                                           multiplier:1.0
                                                             constant:180]];
        [controlBar addConstraint:[NSLayoutConstraint constraintWithItem:control
                                                               attribute:NSLayoutAttributeLeading
                                                               relatedBy:NSLayoutRelationEqual
                                                                  toItem:controlBar
                                                               attribute:NSLayoutAttributeLeading
                                                              multiplier:1.0
                                                                constant:10]];
    }

    NSDictionary *controlViews = @{
        @"popup" : self.serialPortPopUp,
        @"connect" : self.connectButton,
        @"clear" : self.clearButton,
        @"ipFieldLabel" : self.ipAddressFieldLabel,
        @"ipField" : self.ipAddressField,
        @"setIp" : setIpButton,
        @"radarResLabel" : radarDimsControlsLabel,
        @"angularResLabel" : angularSliderLabel,
        @"angularResSlider" : self.angularResSlider,
        @"radialResLabel" : radialSliderLabel,
        @"radialResSlider" : self.radialResSlider
    };

    NSString *verticalLayout = @"V:|-10-[popup]"
                               @"-10-[connect]"
                               @"-5-[clear]"
                               @"-20-[ipFieldLabel]"
                               @"-10-[ipField]"
                               @"-10-[setIp]"
                               @"-30-[radarResLabel]"
                               @"-10-[angularResLabel]"
                               @"-5-[angularResSlider]"
                               @"-10-[radialResLabel]"
                               @"-5-[radialResSlider]";

    [controlBar addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:verticalLayout
                                                                       options:0
                                                                       metrics:nil
                                                                         views:controlViews]];

    // --- populate & finalize ---
    [self.serialPortPopUp addItemsWithTitles:[self findSerialPorts]];

    _radarModel = std::make_shared<RadarModel>();
    _controller = std::make_unique<Controller>(_radarModel);
    _renderer = std::make_unique<Renderer>(device, _radarModel, self.radarView.metalLayer);

    self.renderTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                        target:self
                                                      selector:@selector(mainTick:)
                                                      userInfo:nil
                                                       repeats:YES];
}

- (void)toggleConnection:(id)sender
{
    return;
    /*
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
    */
}

- (void)setRadarIP:(id)sender
{
    _controller->set_radar_unit_ip([self.ipAddressField.stringValue UTF8String]);
}

- (void)clearDetections:(id)sender
{
    if (_radarModel)
    {
        _radarModel->clear_hits();
    }
}

- (void)resSliderChanged:(NSSlider *)sender
{
    NSInteger angular = self.angularResSlider.integerValue;
    NSInteger radial = self.radialResSlider.integerValue;
    _radarModel->change_resolution((uint32_t)angular, (uint32_t)radial);
}

- (void)mainTick:(NSTimer *)timer
{
    @autoreleasepool
    {
        if (_controller)
        {
            _controller->tick();
        }
        if (_renderer)
        {
            _renderer->draw();
        }
    }
}

- (void)setupMainMenu
{
    NSMenu *mainMenu = [[NSMenu alloc] init];

    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appMenuItem];
    [NSApp setMainMenu:mainMenu];

    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Radar"];

    NSString *appName = [[NSProcessInfo processInfo] processName];
    NSString *quitTitle = [NSString stringWithFormat:@"Quit %@", appName];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                       action:@selector(terminate:)
                                                keyEquivalent:@"q"];
    [appMenu addItem:quitItem];

    [appMenuItem setSubmenu:appMenu];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    if (self.renderTimer != nil)
    {
        [self.renderTimer invalidate];
        self.renderTimer = nil;
    }
    
    _controller.reset();
    _renderer.reset();
    _radarModel.reset();
    
    return NSTerminateNow;
}


- (void)applicationWillTerminate:(NSNotification *)notification
{
    return;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

@end

Platform::Platform()
{
    @autoreleasepool
    {
        NSApplication.sharedApplication.activationPolicy = NSApplicationActivationPolicyRegular;
        m_delegate = [AppDelegate new];
        NSApplication.sharedApplication.delegate = m_delegate;
        [NSApp activateIgnoringOtherApps:YES];
    }
}
Platform::~Platform() = default;
void Platform::run()
{
    [NSApp run];
}
