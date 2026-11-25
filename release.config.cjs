module.exports = {
  branches: ["main"],
  tagFormat: "v${version}",
  plugins: [
    ["@semantic-release/commit-analyzer", { preset: "conventionalcommits" }],
    ["@semantic-release/release-notes-generator", { preset: "conventionalcommits" }],
    ["@semantic-release/exec", {
      prepareCmd: 'node scripts/verify-release.mjs "${nextRelease.version}"',
    }],
    ["@semantic-release/github", {
      assets: [
        {
          path: "dist/Verseb-*.zip",
        },
        {
          path: "dist/Verseb-*.zip.sha256",
          label: "SHA-256",
        },
      ],
      successCommentCondition: false,
      failCommentCondition: false,
      releasedLabels: false,
    }],
  ],
};
