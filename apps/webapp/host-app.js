// Injected into every site, in the program's own world (the page can't see it), after
// qwebchannel.js: the host object, and two helpers for the site's scripts in this world.
//
//   webappSettings(callback)   callback(settings) now and whenever the shell changes them
//   webappFetch(url)           a GET through the program, for the addresses site.json allows
//                              (the page's own rules keep the page from asking itself);
//                              resolves to { status, text }
(() => {
    if (globalThis.webappHost)
        return;
    let resolveHost;
    globalThis.webappHost = new Promise(resolve => { resolveHost = resolve; });
    const pending = new Map();
    let nextRequest = 1;

    new QWebChannel(qt.webChannelTransport, channel => {
        const host = channel.objects.host;
        host.got.connect((requestId, status, text) => {
            const done = pending.get(requestId);
            if (done) {
                pending.delete(requestId);
                done({ status: status, text: text });
            }
        });
        resolveHost(host);
    });

    globalThis.webappSettings = callback => webappHost.then(host => {
        callback(host.settings || {});
        host.settingsChanged.connect(() => callback(host.settings || {}));
    });

    globalThis.webappFetch = url => webappHost.then(host => new Promise(done => {
        const requestId = nextRequest++;
        pending.set(requestId, done);
        host.get(requestId, url);
    }));
})();
