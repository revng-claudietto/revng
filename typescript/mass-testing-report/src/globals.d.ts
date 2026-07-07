/*
 * This file is distributed under the MIT License. See LICENSE.md for details.
 */

// Side-effect style imports are handled by webpack's style-loader/css-loader
// (and sass-loader for .scss); these declarations just tell TypeScript such
// imports are valid.
declare module "*.css";
declare module "*.scss";
