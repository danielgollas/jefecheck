// Filter the cards on a list page by search text and area chip. Works offline (file://).
document.addEventListener("DOMContentLoaded", () => {
  const input = document.querySelector(".search");
  const cards = [...document.querySelectorAll(".card")];
  const chips = [...document.querySelectorAll(".chip[data-area]")];
  const empty = document.querySelector(".empty");
  if (!input || !cards.length) return;
  let area = "";
  const apply = () => {
    const words = input.value.toLowerCase().split(/\s+/).filter(Boolean);
    let shown = 0;
    for (const card of cards) {
      const text = card.dataset.search || "";
      const areas = (card.dataset.areas || "").split(" ");
      const ok = words.every((w) => text.includes(w)) &&
        (!area || areas.some((a) => a === area || a.startsWith(area + "/")));
      card.hidden = !ok;
      if (ok) shown++;
    }
    // Older releases' cards are on the landing page for search; show them only while searching.
    const list = document.querySelector(".cards");
    if (list) list.classList.toggle("searching", words.length > 0 || area !== "");
    if (empty) empty.hidden = shown > 0;
  };
  input.addEventListener("input", apply);
  for (const chip of chips) {
    chip.addEventListener("click", () => {
      area = area === chip.dataset.area ? "" : chip.dataset.area;
      chips.forEach((c) => c.classList.toggle("on", c.dataset.area === area));
      apply();
    });
  }
});
