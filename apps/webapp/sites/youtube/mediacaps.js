// Runs inside youtube.com/tv, in the page's own JavaScript world, before the page's scripts.
//
// YouTube's TV player asks MediaSource.isTypeSupported() about more than the codec:
// "…; width=3840; height=2160; framerate=60; bitrate=…; eotf=…". A TV's browser says no to what
// it can't show. Chromium ignores those extra parameters and says yes to everything, also to
// the player's trick questions (width=99999, eotf=catavision), so the player trusts nothing it
// hears and stays at 720p. This answers the extra parameters honestly: up to the screen's real
// resolution, 60 frames a second, no HDR.
(() => {
    const MediaSourceType = window.MediaSource;
    if (!MediaSourceType || MediaSourceType.webappHonest)
        return;

    const original = MediaSourceType.isTypeSupported.bind(MediaSourceType);
    const scale = window.devicePixelRatio || 1;
    const maxWidth = Math.round(screen.width * scale);
    const maxHeight = Math.round(screen.height * scale);
    const maxFramerate = 60;
    const maxBitrate = 200000000;

    MediaSourceType.isTypeSupported = type => {
        const parts = String(type).split(";").map(part => part.trim());
        const kept = [parts[0]];
        for (const part of parts.slice(1)) {
            const at = part.indexOf("=");
            const name = (at < 0 ? part : part.slice(0, at)).trim().toLowerCase();
            const value = at < 0 ? "" : part.slice(at + 1).trim().replace(/^"|"$/g, "");
            const number = Number(value);
            switch (name) {
            case "codecs":
                kept.push(part);
                break;
            case "width":
                if (!(number > 0 && number <= maxWidth))
                    return false;
                break;
            case "height":
                if (!(number > 0 && number <= maxHeight))
                    return false;
                break;
            case "framerate":
                if (!(number > 0 && number <= maxFramerate))
                    return false;
                break;
            case "bitrate":
                if (!(number > 0 && number <= maxBitrate))
                    return false;
                break;
            case "eotf":
                if (value !== "bt709")
                    return false;
                break;
            case "cryptoblockformat":
                if (value !== "subsample")
                    return false;
                break;
            case "decode-to-texture":
                if (value !== "false")
                    return false;
                break;
            case "channels":
                if (!(number > 0 && number <= 2))
                    return false;
                break;
            default:
                // Anything else the player may ask about: leave it to the browser.
                kept.push(part);
            }
        }
        return original(kept.join("; "));
    };
    MediaSourceType.webappHonest = true;
})();
