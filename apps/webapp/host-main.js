// Injected into every site, in the page's own world, before the page's scripts: what the
// program asks when a site's scripts don't say otherwise (DEVELOPMENT.md, "What a site's
// scripts may define"). A site's script replaces parts with Object.assign(webapp, {...}).
(() => {
    if (globalThis.webapp)
        return;
    globalThis.webapp = {
        // Asked once a second: whether something plays, and what kind of thing is loaded:
        // "song", "music-video", "podcast", "video", or "" for nothing. The shell's Settings say which kinds
        // keep playing when the app is left. coverOnly: it shows nothing but a cover. `mark`,
        // if a site gives it, is its own name for the kind, for the log.
        media() {
            const media = Array.from(document.querySelectorAll("video, audio"));
            const playing = media.some(m => !m.paused && !m.ended);
            return { playing: playing, kind: media.length > 0 ? "video" : "", coverOnly: false, mark: "" };
        },
        // Continue what leaving paused (the shell's Resume): true if something was there to play.
        play() {
            const paused = Array.from(document.querySelectorAll("video, audio")).find(m => m.paused && !m.ended);
            if (!paused)
                return false;
            paused.play();
            return true;
        },
        // Back pressed: "page" gives the page Escape, "leave" gives the screen to the shell.
        back() {
            return "page";
        },
    };
})();
