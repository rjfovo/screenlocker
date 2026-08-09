import QtQuick 6.0
import QtMultimedia 6.0

// 锁屏视频壁纸组件：全屏铺满，自动循环播放。
// 独立成文件，仅在 backgroundType=2 时由 LockScreen.qml 动态加载，
// 避免没有 Qt Multimedia 模块的系统在锁屏 QML 编译时失败。
Item {
    id: root
    anchors.fill: parent

    // 由宿主 Loader 绑定到 System.Wallpaper.path
    property url source: ""

    Video {
        id: video
        anchors.fill: parent
        source: root.source
        autoPlay: true
        muted: true   // 锁屏不需要声音
        fillMode: VideoOutput.PreserveAspectCrop
        clip: true

        // 动态切换壁纸（source 变化）时，autoPlay 不会重新触发，需手动播放
        onSourceChanged: {
            if (source.toString() !== "")
                play()
        }

        // 无缝循环：接近结尾 seek 回开头，避免闪烁
        onPositionChanged: {
            if (duration > 0 && (duration - position) < 500)
                position = 0   // Qt6: position 是属性，直接赋值实现 seek
        }

        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.StoppedState)
                play()
        }
    }
}
