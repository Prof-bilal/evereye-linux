import { expect, test } from "@playwright/test";

test("page renders without overflow or browser errors", async ({ page }, testInfo) => {
  const errors: string[] = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("heading", { level: 1 })).toHaveText(/Your machine\.\s*A little more time\./);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
  await expect(page.locator(".app-preview")).toBeVisible();
  await page.screenshot({ path: testInfo.outputPath("landing.png"), fullPage: true });
  expect(errors).toEqual([]);
});

test("preview supports end, restart, custom time and validation", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "End session", exact: true }).click();
  await expect(page.getByTestId("demo-timer")).toHaveText("00:00:00");
  await page.getByRole("button", { name: "Start 15 minutes in preview" }).click();
  await expect(page.getByTestId("demo-timer")).toHaveText(/00:1[45]:\d{2}/);
  await page.getByRole("button", { name: "+ Custom duration" }).click();
  await page.getByLabel("days", { exact: true }).fill("30");
  await page.getByLabel("hours", { exact: true }).fill("1");
  await page.getByRole("button", { name: "Add time", exact: true }).click();
  await expect(page.locator(".demo-error")).toContainText("1 minute to 30 days");
  await page.getByLabel("days", { exact: true }).fill("0");
  await page.getByLabel("hours", { exact: true }).fill("1");
  await page.getByLabel("minutes", { exact: true }).fill("0");
  await page.getByRole("button", { name: "Add time", exact: true }).click();
  await expect(page.locator(".demo-error")).toHaveCount(0);
  await expect(page.getByTestId("demo-timer")).toHaveText(/01:1[45]:\d{2}/);
});

test("installation, FAQ, guide and source download are usable", async ({ page, request }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Fedora", exact: true }).click();
  await expect(page.locator(".command").first()).toContainText("sudo dnf install");
  await page.getByRole("button", { name: "Arch Linux", exact: true }).click();
  await expect(page.locator(".command").first()).toContainText("sudo pacman");
  await page.getByRole("button", { name: "Copy dependency command" }).click();
  await expect(page.getByRole("status")).toHaveText(/Copied|Select and copy/);
  const screenLockFaq = page.locator("details").filter({ hasText: "Can my screen still lock?" });
  await screenLockFaq.locator("summary").click();
  await expect(page.locator("details[open]")).toContainText("does not request screen-lock");
  await page.getByRole("link", { name: "Read the quick-start guide" }).click();
  await expect(page.getByRole("heading", { level: 1 })).toContainText("A little more time.");
  const archive = await request.get("/downloads/evereye-0.4.0-source.tar.gz");
  expect(archive.ok()).toBe(true);
  expect((await archive.body()).subarray(0, 2).toString("hex")).toBe("1f8b");
  expect((await request.get("/license.txt")).ok()).toBe(true);
});

test("keyboard skip link reaches the main content", async ({ page }) => {
  await page.goto("/");
  await page.keyboard.press("Tab");
  await expect(page.getByRole("link", { name: "Skip to content" })).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page).toHaveURL(/#main$/);
});

test("eye illustration, theme and chapter links work with keyboard", async ({ page }) => {
  await page.goto("/");
  const eye = page.getByRole("button", { name: "Wake the eye illustration" });
  await eye.focus();
  await page.keyboard.press("Enter");
  await expect(page.getByRole("button", { name: "Let the illustration rest" })).toHaveAttribute("aria-pressed", "true");
  const theme = page.getByRole("button", { name: "Toggle daylight theme" });
  await theme.click();
  await expect(page.locator("html")).toHaveAttribute("data-theme", "day");
  await theme.click();
  await expect(page.locator("html")).toHaveAttribute("data-theme", "night");
  await page.getByRole("navigation", { name: "Page chapters" }).getByRole("link", { name: /03\s*battery/ }).click();
  await expect(page).toHaveURL(/#features$/);
});
