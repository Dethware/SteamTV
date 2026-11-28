// Runs inside youtube.com/tv, in the program's own JavaScript world (the page's scripts don't
// see it). Skips the parts of a video that SponsorBlock (sponsor.ajay.app) lists, for the
// categories chosen in the shell's Settings, and says so briefly.
//
// The video is the one in the address (#/watch?v=<id>). SponsorBlock is asked by the first
// four hex characters of the ID's SHA-256, so it doesn't learn which video it is, and the
// video's entry is picked from the answer. The request goes through the program
// (webappFetch, host-app.js): the page's own rules don't let it reach sponsor.ajay.app.
(() => {
    if (globalThis.webappSponsorSkip)
        return;
    globalThis.webappSponsorSkip = true;

    const names = {
        sponsor: "sponsor",
        selfpromo: "self-promotion",
        interaction: "reminder",
        intro: "intro",
        outro: "outro",
        preview: "recap",
        music_offtopic: "non-music part",
        filler: "filler",
    };
    // The shell's default, until its settings arrive (or when it has saved none).
    let categories = ["sponsor"];
    let videoId = "";
    let segments = [];
    let generation = 0;
    let hideNote = 0;

    function note(text) {
        let box = document.getElementById("webapp-skip-note");
        if (!box) {
            box = document.createElement("div");
            box.id = "webapp-skip-note";
            box.style.cssText = "position:fixed;right:4vw;bottom:16vh;z-index:2147483647;"
                + "padding:1.2vh 2vh;border-radius:1.2vh;background:rgba(16,20,24,.9);color:#fff;"
                + "font:600 2.6vh sans-serif;transition:opacity .2s;pointer-events:none";
            document.documentElement.appendChild(box);
        }
        box.textContent = text;
        box.style.opacity = "1";
        clearTimeout(hideNote);
        hideNote = setTimeout(() => { box.style.opacity = "0"; }, 3000);
    }

    function addressVideo() {
        const match = /#\/watch\?(?:[^#]*&)?v=([\w-]{11})/.exec(location.href);
        return match ? match[1] : "";
    }

    async function load() {
        const id = videoId;
        const current = ++generation;
        segments = [];
        if (!id || categories.length === 0)
            return;
        const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(id));
        const prefix = Array.from(new Uint8Array(digest).slice(0, 2), b => b.toString(16).padStart(2, "0")).join("");
        const answer = await webappFetch("https://sponsor.ajay.app/api/skipSegments/" + prefix
            + "?categories=" + encodeURIComponent(JSON.stringify(categories))
            + "&actionTypes=" + encodeURIComponent(JSON.stringify(["skip"])));
        if (current !== generation)
            return;
        if (answer.status === 404) { // nothing for any video with this hash prefix
            console.log("webapp: video " + id + " has no segments");
            return;
        }
        if (answer.status !== 200) {
            console.log("webapp: can't ask SponsorBlock: " + answer.status);
            return;
        }
        const found = [];
        try {
            for (const video of JSON.parse(answer.text)) {
                if (video.videoID !== id)
                    continue;
                for (const entry of video.segments || []) {
                    const span = entry.segment || [];
                    if (span.length === 2 && span[1] > span[0])
                        found.push({ start: span[0], end: span[1], category: entry.category });
                }
            }
        } catch (e) {
            console.log("webapp: SponsorBlock's answer unreadable: " + e);
        }
        segments = found;
        console.log("webapp: video " + id + " has " + found.length + " segments to skip");
    }

    // A list saved with one entry reads back as a string, an empty one as null.
    webappSettings(settings => {
        const value = settings.skipCategories;
        categories = value === undefined ? ["sponsor"] : [].concat(value || []);
        load();
    });

    setInterval(() => {
        const id = addressVideo();
        if (id !== videoId) {
            videoId = id;
            load();
        }
        if (segments.length === 0)
            return;
        const video = document.querySelector("video.html5-main-video") || document.querySelector("video");
        if (!video || video.paused || !isFinite(video.duration))
            return;
        const now = video.currentTime;
        for (const segment of segments) {
            // Not the last moments of a segment: seeking lands slightly early at times.
            if (now >= segment.start && now < segment.end - 0.5) {
                video.currentTime = Math.min(segment.end, video.duration);
                note("Skipped " + (names[segment.category] || segment.category));
                console.log("webapp: skipped " + segment.category + " " + segment.start + "-" + segment.end);
                break;
            }
        }
    }, 250);
})();
