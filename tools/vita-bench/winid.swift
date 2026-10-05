import CoreGraphics
import Foundation
// prints the window id of the first on-screen window whose owner PID is argv[1]
let pid = Int32(CommandLine.arguments[1])!
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
for w in list {
    if (w[kCGWindowOwnerPID as String] as? Int32) == pid, (w[kCGWindowLayer as String] as? Int) == 0,
       let b = w[kCGWindowBounds as String] as? [String: Any], (b["Height"] as? Double ?? 0) > 300 {
        print(w[kCGWindowNumber as String] as! Int); break
    }
}
