// JefeCheck marketing site — interactions (translated from Claude Design .dc logic)
(function () {
  // Mobile menu toggle
  var btn = document.querySelector('.nav-hamburger');
  var menu = document.querySelector('.nav-mobile-menu');
  if (btn && menu) {
    btn.addEventListener('click', function () { menu.classList.toggle('open'); });
    menu.querySelectorAll('a').forEach(function (a) {
      a.addEventListener('click', function () { menu.classList.remove('open'); });
    });
    window.addEventListener('resize', function () {
      if (window.innerWidth >= 880) menu.classList.remove('open');
    });
  }

  // Copy-to-clipboard install commands
  document.querySelectorAll('.copy-btn').forEach(function (b) {
    b.addEventListener('click', function () {
      var card = b.closest('.cmd-card');
      var el = card && card.querySelector('.cmd-text');
      var cmd = el ? el.getAttribute('data-cmd') : '';
      if (navigator.clipboard && cmd) { navigator.clipboard.writeText(cmd).catch(function () {}); }
      var original = b.textContent;
      b.textContent = 'Copied!';
      setTimeout(function () { b.textContent = original; }, 1600);
    });
  });

  // Troubleshooting accordion
  var tBtn = document.querySelector('.trouble-toggle');
  var tBody = document.querySelector('.trouble-body');
  var tSign = document.querySelector('.trouble-sign');
  if (tBtn && tBody) {
    tBtn.addEventListener('click', function () {
      var open = tBody.classList.toggle('open');
      if (tSign) tSign.textContent = open ? '−' : '+';
    });
  }

  // Demo video click-to-play
  var frame = document.querySelector('.demo-frame');
  var video = document.querySelector('.demo-video');
  var overlay = document.querySelector('.demo-overlay');
  if (frame && video) {
    frame.addEventListener('click', function () {
      if (overlay) overlay.classList.add('hidden');
      video.controls = true;
      video.play();
    });
  }
})();
