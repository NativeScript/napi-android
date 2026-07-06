const process = require("node:process");
const semver = require("semver");

let currentVersion = process.env.NPM_VERSION;
if (!currentVersion) {
  const cmdArgs = process.argv.slice(2);
  const target = cmdArgs[0] || "android";
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

  currentVersion = require(packageJsonByTarget[target]).version;
}

function validateNpmTag(version) {
  const parsed = semver.parse(version);
  return (
    parsed.prerelease.length === 0 ||
    (typeof parsed.prerelease[0] === "string" &&
      /^[a-zA-Z][0-9A-Za-z-]*$/.test(parsed.prerelease[0]))
  );
}

function getNpmTag(version) {
  if (!validateNpmTag(version)) {
    throw new Error(`Invalid npm tag "${version}"`);
  }
  const parsed = semver.parse(version);
  return parsed.prerelease[0] || "latest";
}

console.log(getNpmTag(currentVersion));
