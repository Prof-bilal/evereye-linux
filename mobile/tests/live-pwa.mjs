// Drive the real PWA against a real `evereye gateway` and daemon.
// Invoked by linux/scripts/test-pwa-live.sh with BASE_URL and TOKEN set.
import { chromium } from "@playwright/test";

const base = process.env.BASE_URL;
const token = process.env.TOKEN;
if (!base || !token) {
  console.error("BASE_URL and TOKEN are required");
  process.exit(2);
}

const fail = (message) => {
  console.error("FAIL:", message);
  process.exit(1);
};

const browser = await chromium.launch();
try {
  const context = await browser.newContext();
  await context.addInitScript(
    (config) => localStorage.setItem("evereye.mobile.config", JSON.stringify(config)),
    { url: base, token }
  );
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));

  await page.goto(base, { waitUntil: "networkidle" });
  const countdown = page.getByTestId("countdown");
  if (!(await countdown.isVisible())) fail("countdown did not render");
  if (errors.length) fail("page errors: " + errors.join("; "));

  // Start a preset job through the real gateway.
  await page.getByRole("button", { name: "Start session" }).click();

  // The new job appears in the list; select it and read the live stream.
  const job = page.getByRole("button", { name: /live-preset/ });
  await job.first().waitFor({ timeout: 15000 });
  await job.first().click();
  await page.getByTestId("log").getByText(/live-pwa-marker/).waitFor({ timeout: 15000 });

  console.log("ok - real PWA streamed job output through the real gateway");
  if (errors.length) fail("page errors: " + errors.join("; "));
} finally {
  await browser.close();
}
