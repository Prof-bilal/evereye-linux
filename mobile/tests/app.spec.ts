import { expect, test, type Page } from "@playwright/test";

const config = { url: "http://127.0.0.1:3108", token: "test-token" };

test.beforeEach(async ({ request }) => {
  await request.post("http://127.0.0.1:3108/api/__reset");
});

async function connect(page: Page) {
  await page.addInitScript((value) => {
    localStorage.setItem("evereye.mobile.config", JSON.stringify(value));
  }, config);
  await page.goto("/");
}

test("shows the live session and jobs", async ({ page }) => {
  await connect(page);
  await expect(page.getByTestId("countdown")).toHaveText("01:02:05");
  await expect(page.getByText("Active", { exact: true })).toBeVisible();
  await expect(page.getByRole("button", { name: /claude/ })).toBeVisible();
});

test("selecting a job streams its output", async ({ page }) => {
  await connect(page);
  await page.getByRole("button", { name: /claude/ }).first().click();
  await expect(page.getByTestId("log")).toContainText("hello from mock");
});

test("ending the session clears the active state", async ({ page }) => {
  await connect(page);
  await page.getByRole("button", { name: "End session" }).click();
  await expect(page.getByTestId("countdown")).toHaveText("00:00:00");
  await expect(page.getByText("Idle", { exact: true })).toBeVisible();
});

test("starting a preset adds a job", async ({ page }) => {
  await connect(page);
  await expect(page.getByRole("button", { name: /claude/ })).toHaveCount(1);
  await page.getByRole("button", { name: "Start session" }).click();
  await expect(page.getByRole("button", { name: /claude/ })).toHaveCount(2);
});

test("setup saves a gateway and survives reload", async ({ page }) => {
  await page.goto("/");
  await page.getByLabel("Gateway URL").fill(config.url);
  await page.getByLabel("Token").fill(config.token);
  await page.getByRole("button", { name: "Connect" }).click();
  await expect(page.getByTestId("countdown")).toHaveText("01:02:05");
  await page.reload();
  await expect(page.getByTestId("countdown")).toBeVisible();
});

test("rejects a bad token without hiding the setup form", async ({ page }) => {
  await page.goto("/");
  await page.getByLabel("Gateway URL").fill(config.url);
  await page.getByLabel("Token").fill("wrong");
  await page.getByRole("button", { name: "Connect" }).click();
  await expect(page.getByText("Token rejected")).toBeVisible();
});
