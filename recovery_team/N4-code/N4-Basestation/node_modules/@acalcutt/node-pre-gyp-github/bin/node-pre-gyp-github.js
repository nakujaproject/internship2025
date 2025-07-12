#!/usr/bin/env node

import NodePreGypGithub from '../index.js';
import { program } from "commander";

program
  .command("publish")
  .description("publishes the contents of ./build/stage/{version} to the current version's GitHub release")
  .option("-r, --release", "publish immediately, do not create draft")
  .option("-s, --silent", "turns verbose messages off")
  .action(async (options) => {
    const opts = {
      draft: !options.release,
      verbose: !options.silent,
    };
    try {
      const nodePreGypGithub = new NodePreGypGithub();
      await nodePreGypGithub.publish(opts);
    } catch (err) {
      console.error(`An error occurred whilst publishing:`, err);
      process.exit(1);
    }
  });

await program.parseAsync(process.argv);