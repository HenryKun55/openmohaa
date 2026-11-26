// vinput: sends keyboard and mouse input to one process (Vita3K) without moving the
// real cursor or taking focus: CGEvent.postToPid. Vita3K maps its keyboard to the pad
// (config.yml: I/J/K/L right stick, W/A/S/D left stick) and a left-button drag on the
// window to the front touch panel.
//
//   vinput <pid> <command> [<command> ...]
//     key <keycode> <ms>          hold a key (macOS virtual keycode) for ms
//     drag <x0> <y0> <x1> <y1> <ms>  left-button drag, coordinates are fractions (0..1)
//                                 of the window's content area (below the title bar)
//     wait <ms>
//     focus                       bring Vita3K to the front: its Qt game window turns a mouse
//                                 drag into touch only while focused (touch.cpp renderer_focused)
import AppKit
import CoreGraphics
import Foundation

let args = Array(CommandLine.arguments.dropFirst())
guard let pid = Int32(args.first ?? "") else {
    FileHandle.standardError.write("usage: vinput <pid> commands...\n".data(using: .utf8)!)
    exit(2)
}

var windowNumber: Int64 = 0

func contentRect() -> CGRect? {
    let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
    for w in list {
        if (w[kCGWindowOwnerPID as String] as? Int32) == pid, (w[kCGWindowLayer as String] as? Int) == 0,
           let b = w[kCGWindowBounds as String] as? [String: Any], (b["Height"] as? Double ?? 0) > 300 {
            windowNumber = Int64(w[kCGWindowNumber as String] as! Int)
            let x = b["X"] as! Double, y = b["Y"] as! Double
            let width = b["Width"] as! Double, height = b["Height"] as! Double
            let title = 28.0  // macOS title bar
            return CGRect(x: x, y: y + title, width: width, height: height - title)
        }
    }
    return nil
}

func sleepMs(_ ms: Double) { usleep(useconds_t(max(0, ms) * 1000)) }

func mouse(_ type: CGEventType, _ p: CGPoint) {
    guard let e = CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: p, mouseButton: .left) else { return }
    // the target window, or AppKit drops a mouse event posted to a process
    e.setIntegerValueField(.mouseEventWindowUnderMousePointer, value: windowNumber)
    e.setIntegerValueField(.mouseEventWindowUnderMousePointerThatCanHandleThisEvent, value: windowNumber)
    e.setIntegerValueField(.mouseEventClickState, value: 1)
    e.postToPid(pid)
}

var i = 1
while i < args.count {
    switch args[i] {
    case "key":
        let code = CGKeyCode(args[i + 1])!, ms = Double(args[i + 2])!
        CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: true)?.postToPid(pid)
        sleepMs(ms)
        CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: false)?.postToPid(pid)
        i += 3
    case "drag":
        guard let r = contentRect() else { print("no window"); exit(1) }
        let v = (1...5).map { Double(args[i + $0])! }
        let a = CGPoint(x: r.minX + v[0] * r.width, y: r.minY + v[1] * r.height)
        let b = CGPoint(x: r.minX + v[2] * r.width, y: r.minY + v[3] * r.height)
        let steps = max(1, Int(v[4] / 16))
        mouse(.leftMouseDown, a)
        sleepMs(50)
        for s in 1...steps {
            let t = Double(s) / Double(steps)
            mouse(.leftMouseDragged, CGPoint(x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t))
            sleepMs(16)
        }
        sleepMs(50)
        mouse(.leftMouseUp, b)
        i += 6
    case "focus":
        NSRunningApplication(processIdentifier: pid)?.activate(options: [.activateIgnoringOtherApps])
        sleepMs(400)
        i += 1
    case "wait":
        sleepMs(Double(args[i + 1])!)
        i += 2
    default:
        print("unknown command \(args[i])")
        exit(2)
    }
}
