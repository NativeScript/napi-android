const semver = require("semver");
const child_process = require("child_process");
const dayjs = require("dayjs");
const fs = require("fs");

function resolveCurrentVersion() {
  if (process.env.NPM_VERSION) {
    return process.env.NPM_VERSION;
  }
  const target = process.argv[2] || "android";
  const packageJsonByTarget = {
    android: "../package.json",
    "android-v8": "../packages/android-v8/package.json",
    "android-hermes": "../packages/android-hermes/package.json",
    "android-jsc": "../packages/android-jsc/package.json",
    "android-quickjs": "../packages/android-quickjs/package.json",
    "android-quickjs-ng": "../packages/android-quickjs-ng/package.json",
    "android-primjs": "../packages/android-primjs/package.json",
  };

  if (!packageJsonByTarget[target]) {
    throw new Error(
      `Unknown target "${target}". Expected one of ${Object.keys(packageJsonByTarget)
        .map((name) => `"${name}"`)
        .join(", ")}.`,
    );
  }

  return require(packageJsonByTarget[target]).version;
}

const currentVersion = resolveCurrentVersion();

if (!currentVersion) {
  throw new Error("Invalid current version");
}
const currentTag = process.env.NPM_TAG || "next";
const runID = process.env.GITHUB_RUN_ID || 0;

let prPrerelease = "";

if (currentTag === "pr" && process.env.GITHUB_EVENT_PATH) {
  try {
    const ev = JSON.parse(fs.readFileSync(process.env.GITHUB_EVENT_PATH, "utf8"));
    const prNum = ev.pull_request.number;
    // add extra PR number to version-pr.PRNUM-....
    prPrerelease = `${prNum}-`;
  } catch (e) {
    // don't add pr prerelease
  }
}

const preRelease = `${currentTag}.${prPrerelease}${dayjs().format("YYYY-MM-DD")}-${runID}`;

let lastTagVersion = (
  process.env.LAST_TAGGED_VERSION ||
  child_process
    .spawnSync("git", ["describe", "--tags", "--abbrev=0", "--match=v*"])
    .stdout.toString()
)
  .trim()
  .substring(1);
if (!semver.parse(lastTagVersion)) {
  throw new Error("Invalid last tag version");
}

function setPreRelease(version) {
  const parsed = semver.parse(version);
  return semver.parse(
    `${parsed.major}.${parsed.minor}.${parsed.patch}-${preRelease}`
  );
}

let nextVersion = setPreRelease(currentVersion);

if (!nextVersion) {
  throw new Error("Invalid next version");
}

if (semver.compare(currentVersion, lastTagVersion) <= 0) {
  // next version is older than current version
  nextVersion = setPreRelease(semver.parse(lastTagVersion).inc("patch"));
}

if (!nextVersion) {
  throw new Error("Invalid next version");
}

console.log(nextVersion.format());
