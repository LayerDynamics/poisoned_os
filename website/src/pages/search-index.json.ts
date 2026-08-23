import { getCollection } from "astro:content";
import type { APIRoute } from "astro";
import { sortDocs, toRepositoryDoc } from "../lib/docs";

export const prerender = true;

export const GET: APIRoute = async () => {
  const docs = sortDocs((await getCollection("docs")).map(toRepositoryDoc));
  return new Response(JSON.stringify(docs.map(({ title, description, section, sourcePath, slug, plainText }) => ({ title, description, section, sourcePath, slug, text: plainText }))), {
    headers: { "Content-Type": "application/json; charset=utf-8" },
  });
};
