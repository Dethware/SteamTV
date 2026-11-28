// youtube.com/tv's part of globalThis.webapp (host-main.js), in the page's own world: it calls
// the player's getPlayerResponse(). Title and artist need nothing here: YouTube sets them in
// navigator.mediaSession, and Chromium's MPRIS player carries them to the shell.
(() => {
    if (!globalThis.webapp || globalThis.webapp.youtube)
        return;
    // YouTube's own mark on a video (musicVideoType) as one of the kinds the shell's Settings
    // know: "song", "music-video", "podcast" or "video". No mark is an ordinary video; so is
    // a mark not listed here.
    const kinds = {
        MUSIC_VIDEO_TYPE_ATV: "song",                   // a song showing only its cover
        MUSIC_VIDEO_TYPE_PRIVATELY_OWNED_TRACK: "song", // one's own upload to YouTube Music
        MUSIC_VIDEO_TYPE_OMV: "music-video",            // an official music video
        MUSIC_VIDEO_TYPE_UGC: "music-video",            // music uploaded by a user
        MUSIC_VIDEO_TYPE_OFFICIAL_SOURCE_MUSIC: "music-video",
        MUSIC_VIDEO_TYPE_PODCAST_EPISODE: "podcast",
    };

    // Whether the player offers "Switch to audio" for the open video: its actions include
    // TRANSPORT_CONTROLS_BUTTON_TYPE_MUSIC_DISPLAY. YouTube offers it for music it hasn't put
    // a mark on too (an extended track uploaded as an ordinary video), and not for ordinary
    // videos. Read from the player's actions in the page (the container's own data, there
    // while the controls are hidden too); if YouTube changes that, it is simply not found.
    function hasMusicDisplay() {
        try {
            const actions = document.querySelector("ytlr-player-actions-container").__instance.props.data.settingActions;
            return (actions || []).some(action => action.type === "TRANSPORT_CONTROLS_BUTTON_TYPE_MUSIC_DISPLAY");
        } catch (e) {
            return false;
        }
    }

    Object.assign(globalThis.webapp, {
        youtube: true,

        // The main video, while one is open (#/watch). Its kind is YouTube's mark; with no mark
        // (or one not listed), a video with the music display is a music video, any other an
        // ordinary video.
        media() {
            const video = document.querySelector("video.html5-main-video");
            if (!video || !/#\/watch/.test(location.hash))
                return { playing: false, kind: "", coverOnly: false, mark: "" };
            const player = document.querySelector(".html5-video-player");
            let mark = "";
            try {
                const response = player && player.getPlayerResponse && player.getPlayerResponse();
                mark = (response && response.videoDetails && response.videoDetails.musicVideoType) || "";
            } catch (e) {
            }
            const display = !kinds[mark] && hasMusicDisplay();
            return {
                playing: !video.paused && !video.ended,
                kind: kinds[mark] || (display ? "music-video" : "video"),
                coverOnly: mark === "MUSIC_VIDEO_TYPE_ATV",
                mark: mark || (display ? "no mark, music display" : "no mark"),
            };
        },

        // The main video, if one is open; true if there was one to play.
        play() {
            const video = document.querySelector("video.html5-main-video");
            if (!video || !/#\/watch/.test(location.hash))
                return false;
            if (video.paused)
                video.play();
            return true;
        },

        // On the account picker ("Who's watching?") Back leaves: it has the same address as
        // the home screen, so this looks at the page.
        back() {
            const picker = document.querySelector("ytlr-account-selector");
            if (!picker)
                return "page";
            const box = picker.getBoundingClientRect();
            return box.width > 0 && box.height > 0 ? "leave" : "page";
        },
    });
})();
