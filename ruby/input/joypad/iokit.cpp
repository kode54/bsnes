#pragma once

#include <IOKit/hid/IOHIDLib.h>

auto deviceMatchingCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) -> void;
auto deviceRemovalCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) -> void;

struct InputJoypadIOKit {
  Input& input;
  InputJoypadIOKit(Input& input) : input(input) {}

  struct Joypad {
    auto addElements(CFArrayRef elements) -> void {
      for (auto i : range(CFArrayGetCount(elements))) {
        IOHIDElementRef element = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
        IOHIDElementType type = IOHIDElementGetType(element);
        uint32_t usage = IOHIDElementGetUsage(element);
        switch(type) {
          case kIOHIDElementTypeInput_Button:
            addButton(element);
            break;
          case kIOHIDElementTypeInput_Axis:
          case kIOHIDElementTypeInput_Misc:
            switch(usage) {
              case kHIDUsage_GD_X:
              case kHIDUsage_GD_Y:
              case kHIDUsage_GD_Z:
              case kHIDUsage_GD_Rx:
              case kHIDUsage_GD_Ry:
              case kHIDUsage_GD_Rz:
              case kHIDUsage_GD_Slider:
              case kHIDUsage_GD_Dial:
              case kHIDUsage_GD_Wheel:
              case kHIDUsage_Sim_Accelerator:
              case kHIDUsage_Sim_Brake:
              case kHIDUsage_Sim_Rudder:
              case kHIDUsage_Sim_Throttle:
                addAxis(element);
                break;
              case kHIDUsage_GD_DPadUp:
              case kHIDUsage_GD_DPadDown:
              case kHIDUsage_GD_DPadRight:
              case kHIDUsage_GD_DPadLeft:
              case kHIDUsage_GD_Start:
              case kHIDUsage_GD_Select:
              case kHIDUsage_GD_SystemMainMenu:
                addButton(element);
                break;
              case kHIDUsage_GD_Hatswitch: {
                addHat(element);
                break;
              }
              default:
                break;
            }
          case kIOHIDElementTypeCollection: {
            CFArrayRef moreElements = IOHIDElementGetChildren(element);
            if (moreElements) addElements(moreElements);
            break;
          }
          default:
            break;
        }
      }
    }

    auto addAxis(IOHIDElementRef element) -> void {
      int min = IOHIDElementGetLogicalMin(element);
      int max = IOHIDElementGetLogicalMax(element);
      int range = max - min;
      if (range == 0) return;

      IOHIDElementCookie cookie = IOHIDElementGetCookie(element);
      auto duplicate = axes.find([cookie](auto axis) { return IOHIDElementGetCookie(axis) == cookie; });
      if (duplicate) return;

      hid->axes().append(axes.size());
      axes.append(element);
    }
    auto addButton(IOHIDElementRef element) -> void {
      IOHIDElementCookie cookie = IOHIDElementGetCookie(element);
      auto duplicate = buttons.find([cookie](auto button) { return IOHIDElementGetCookie(button) == cookie; });
      if (duplicate) return;

      hid->buttons().append(buttons.size());
      buttons.append(element);
    }
    auto addHat(IOHIDElementRef element) -> void {
      IOHIDElementCookie cookie = IOHIDElementGetCookie(element);
      auto duplicate = hats.find([cookie](auto hat) { return IOHIDElementGetCookie(hat) == cookie; });
      if (duplicate) return;

      int n = hats.size() * 2;
      hid->hats().append(n);
      hid->hats().append(n + 1);
      hats.append(element);
    }

    shared_pointer<HID::Joypad> hid{new HID::Joypad};

    IOHIDDeviceRef device = nullptr;
    vector<IOHIDElementRef> axes;
    vector<IOHIDElementRef> hats;
    vector<IOHIDElementRef> buttons;
  };
  vector<Joypad> joypads;

  IOHIDManagerRef manager = nullptr;

  enum : int {
    NEUTRAL = 0,
    LEFT = -1,
    RIGHT = 1,
    UP = -1,
    DOWN = 1,
  };

  auto assign(shared_pointer<HID::Joypad> hid, uint groupID, uint inputID, int16_t value) -> void {
    auto& group = hid->group(groupID);
    if(group.input(inputID).value() == value) return;
    input.doChange(hid, groupID, inputID, group.input(inputID).value(), value);
    group.input(inputID).setValue(value);
  }

  auto poll(vector<shared_pointer<HID::Device>>& devices) -> void {
    //hotplugging
    detectDevices();

    for(auto& jp : joypads) {
      IOHIDDeviceRef device = jp.device;
      for(auto n : range(jp.axes.size())) {
        int value = 0;
        IOHIDValueRef valueRef;
        if(IOHIDDeviceGetValue(device, jp.axes[n], &valueRef) == kIOReturnSuccess) {
          int min = IOHIDElementGetLogicalMin(jp.axes[n]);
          int max = IOHIDElementGetLogicalMax(jp.axes[n]);
          int range = max - min;
          value = (IOHIDValueGetIntegerValue(valueRef) - min) * 65535ll / range - 32767;
        }

        assign(jp.hid, HID::Joypad::GroupID::Axis, n, sclamp<16>(value));
      }
      for(auto n : range(jp.buttons.size())) {
        int value = 0;
        IOHIDValueRef valueRef;
        if(IOHIDDeviceGetValue(device, jp.buttons[n], &valueRef) == kIOReturnSuccess) {
          value = IOHIDValueGetIntegerValue(valueRef);
        }

        assign(jp.hid, HID::Joypad::GroupID::Button, n, (bool)value);
      }
      for(auto n : range(jp.hats.size())) {
        int x = NEUTRAL;
        int y = NEUTRAL;

        IOHIDValueRef valueRef;
        if(IOHIDDeviceGetValue(device, jp.hats[n], &valueRef) == kIOReturnSuccess) {
          int position = IOHIDValueGetIntegerValue(valueRef);
          int min = IOHIDElementGetLogicalMin(jp.hats[n]);
          int max = IOHIDElementGetLogicalMax(jp.hats[n]);
          if(position >= min && position <= max) {
            position -= min;
            int range = max - min + 1;
            if (range == 4) position *= 2;
            if (range == 8) {
              switch(position) {
                case 0: x = UP;              break;
                case 1: x = UP;   y = RIGHT; break;
                case 2:           y = RIGHT; break;
                case 3: x = DOWN; y = RIGHT; break;
                case 4: x = DOWN;            break;
                case 5: x = DOWN; y = LEFT;  break;
                case 6:           y = LEFT;  break;
                case 7: x = UP;   y = LEFT;  break;
                default:                     break;
              }
            }
          }
        }

        assign(jp.hid, HID::Joypad::GroupID::Hat, n * 2,     x * 32767);
        assign(jp.hid, HID::Joypad::GroupID::Hat, n * 2 + 1, y * 32767);
      }

      devices.append(jp.hid);
    }
  }

  auto rumble(uint64_t id, bool enable) -> bool {
    //TODO
    return false;
  }

  auto initialize() -> bool {
    manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if(!manager) return false;

    CFArrayRef matcher = createMatcher();
    if(!matcher) {
      releaseManager();
      return false;
    }

    IOHIDManagerSetDeviceMatchingMultiple(manager, matcher);
    IOHIDManagerRegisterDeviceMatchingCallback(manager, deviceMatchingCallback, this);
    IOHIDManagerRegisterDeviceRemovalCallback(manager, deviceRemovalCallback, this);

    if(IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
      releaseManager();
      return false;
    }

    detectDevices();

    return true;
  }

  auto terminate() -> void {
    if(!manager) return;
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    releaseManager();
  }

  auto initJoypad(IOHIDDeviceRef device) -> void {
    Joypad jp;
    jp.device = device;

    int32_t vendorID, productID;
    CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)), kCFNumberSInt32Type, &vendorID);
    CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)), kCFNumberSInt32Type, &productID);
    jp.hid->setVendorID(vendorID);
    jp.hid->setProductID(productID);

    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
    if(elements) {
      jp.addElements(elements);
      CFRelease(elements);

      joypads.append(jp);
    }
  }

  auto removeJoypad(IOHIDDeviceRef device) -> void {
    for(uint n : range(joypads.size())) {
      if(joypads[n].device == device) {
        joypads.remove(n);
        return;
      }
    }
  }

private:
  auto releaseManager() -> void {
    CFRelease(manager);
    manager = nullptr;
  }
  auto createMatcher() -> CFArrayRef {
    CFDictionaryRef dict1 = createMatcherCriteria(kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick);
    if(!dict1) return nullptr;
    CFDictionaryRef dict2 = createMatcherCriteria(kHIDPage_GenericDesktop, kHIDUsage_GD_GamePad);
    if(!dict2) return CFRelease(dict1), nullptr;
    CFDictionaryRef dict3 = createMatcherCriteria(kHIDPage_GenericDesktop, kHIDUsage_GD_MultiAxisController);
    if(!dict3) return CFRelease(dict1), CFRelease(dict2), nullptr;

    const void* vals[3] = { dict1, dict2, dict3 };

    CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, vals, 3, &kCFTypeArrayCallBacks);
    for(auto i : range(3)) CFRelease(vals[i]);
    return array;
  }
  auto createMatcherCriteria(uint32_t page, uint32_t usage) -> CFDictionaryRef {
    CFNumberRef pageNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
    if(!pageNum) return nullptr;

    CFNumberRef usageNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
    if(!usageNum) return CFRelease(pageNum), nullptr;

    const void* keys[2] = { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
    const void* vals[2] = { pageNum, usageNum };

    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    for(auto i : range(2)) CFRelease(vals[i]);
    return dict;
  }
  auto detectDevices() -> void {
    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFStringRef runLoopMode = CFSTR("rubyIOKitJoypad");
    IOHIDManagerScheduleWithRunLoop(manager, runLoop, runLoopMode);
    while(CFRunLoopRunInMode(runLoopMode, 0, true) == kCFRunLoopRunHandledSource);
    IOHIDManagerUnscheduleFromRunLoop(manager, runLoop, runLoopMode);
  }
};

auto deviceMatchingCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) -> void {
  ((InputJoypadIOKit*)context)->initJoypad(device);
}
auto deviceRemovalCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) -> void {
  ((InputJoypadIOKit*)context)->removeJoypad(device);
}
