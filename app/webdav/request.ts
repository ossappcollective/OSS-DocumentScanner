import { request } from '~/services/api';
import { generateDigestAuthHeader, parseDigestAuth } from './auth/digest';
import { mergeHeaders } from './tools/headers';
import { RequestOptions, RequestOptionsWithState, WebDAVClientContext, WebDAVMethodOptions } from './types';

export function prepareRequestOptions(requestOptions: RequestOptions | RequestOptionsWithState, context: WebDAVClientContext, userOptions: WebDAVMethodOptions): RequestOptionsWithState {
    const finalOptions = { ...requestOptions, ...(userOptions || {}) } as RequestOptionsWithState;
    finalOptions.headers = mergeHeaders(context.headers, finalOptions.headers || {}, userOptions.headers || {});
    finalOptions.responseOnMainThread = false;
    finalOptions.body = requestOptions.data as any;

    if (context.digest) {
        finalOptions._digest = context.digest;
    }
    return finalOptions;
}

async function webdavRequest<T = any>(requestOptions: RequestOptionsWithState) {
    // Client not configured for digest authentication
    if (!requestOptions._digest) {
        return request<T>(requestOptions);
    }
    // Remove client's digest authentication object from request options
    const _digest = requestOptions._digest;
    delete requestOptions._digest;
    // If client is already using digest authentication, include the digest authorization header
    if (_digest.hasDigestAuth) {
        requestOptions.headers = requestOptions.headers || {};
        requestOptions.headers.Authorization = generateDigestAuthHeader(requestOptions, _digest);
    }
    // Perform digest request + check
    const response = await request<T>(requestOptions);
    if (response.statusCode === 401) {
        _digest.hasDigestAuth = parseDigestAuth(response, _digest);
        if (_digest.hasDigestAuth) {
            requestOptions.headers = requestOptions.headers || {};
            requestOptions.headers.Authorization = generateDigestAuthHeader(requestOptions, _digest);
            const response2 = await request<T>(requestOptions);
            if (response2.statusCode === 401) {
                _digest.hasDigestAuth = false;
            } else {
                _digest.nc++;
            }
            return response2;
        }
    } else {
        _digest.nc++;
    }
    return response;
}
export { webdavRequest as request };
